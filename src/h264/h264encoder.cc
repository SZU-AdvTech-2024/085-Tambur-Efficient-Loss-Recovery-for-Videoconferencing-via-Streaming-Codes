#include <sys/sysinfo.h>
#include <cassert>
#include <iostream>
#include <string>
#include <stdexcept>
#include <chrono>
#include <algorithm>
#include <limits>

#include "h264encoder.hh"
#include "conversion.hh"
#include "timestamp.hh"

using namespace std;
using namespace chrono;

H264Encoder::H264Encoder(const uint16_t display_width,
                 const uint16_t display_height,
                 const uint16_t frame_rate,
                 const string & output_path)
  : display_width_(display_width), display_height_(display_height),
    frame_rate_(frame_rate), output_fd_(),codec_ctx_(nullptr),codec_(nullptr),frame_(nullptr)
{
  // open the output file
  if (not output_path.empty()) {
    output_fd_ = FileDescriptor(check_syscall(
        open(output_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644)));
  }

  codec_ = avcodec_find_encoder(AV_CODEC_ID_H264);
  codec_ctx_ = avcodec_alloc_context3(codec_);

  // copy the configuration below mostly from WebRTC (libvpx_vp9_encoder.cc)
  codec_ctx_->width = display_width_;
  codec_ctx_->height = display_height_;
  codec_ctx_->time_base = AVRational{1, frame_rate_};
  codec_ctx_->framerate = AVRational{frame_rate_, 1};
  codec_ctx_->thread_count = 4; // encoder threads; should equal to column tiles below
  codec_ctx_->rc_buffer_size = 1000; 
  codec_ctx_->rc_initial_buffer_occupancy = 500; 

  codec_ctx_->gop_size = 20;
  codec_ctx_->max_b_frames = 0;
  codec_ctx_->pix_fmt = AV_PIX_FMT_YUV420P;

  // 设置编码器选项
  avcodec_open2(codec_ctx_, codec_, NULL);
  frame_ = av_frame_alloc();


  frame_->format = codec_ctx_->pix_fmt;
  frame_->width  = codec_ctx_->width;
  frame_->height = codec_ctx_->height;

  av_image_alloc(frame_->data, frame_->linesize,
                     codec_ctx_->width, codec_ctx_->height,
                     codec_ctx_->pix_fmt, 32);

  // use no more than 16 or the number of avaialble CPUs
  const unsigned int cpu_used = min(get_nprocs(), 16);

  cerr << "Initialized H264 encoder (CPU used: " << cpu_used << ")" << endl;
}

H264Encoder::~H264Encoder()
{
  av_frame_free(&frame_);
  avcodec_free_context(&codec_ctx_);
}

void H264Encoder::compress_frame(const RawImage & raw_img)
{
  const auto frame_generation_ts = timestamp_us();

  // encode raw_img into frame 'frame_id_'
  encode_frame(raw_img);

  // packetize frame 'frame_id_' into datagrams
  const size_t frame_size = packetize_encoded_frame();

  // output frame information
  if (output_fd_) {
    const auto frame_encoded_ts = timestamp_us();

    output_fd_->write(to_string(frame_id_) + "," +
                      to_string(target_bitrate_) + "," +
                      to_string(frame_size) + "," +
                      to_string(frame_generation_ts) + "," +
                      to_string(frame_encoded_ts) + "\n");
  }

  // move onto the next frame
  frame_id_++;
}

void H264Encoder::encode_frame(const RawImage & raw_img)
{
  //if (raw_img.display_width() != display_width_ or
  //    raw_img.display_height() != display_height_) {
  //  throw runtime_error("H264Encoder: image dimensions don't match");
  //}
  int ret;
  int encode_flags = 0;
  // check if a key frame needs to be encoded
  if(frame_index_gop_++ % 20 == 1){
    encode_flags = 1;
  }
  if (not unacked_.empty()) {
    const auto & first_unacked = unacked_.cbegin()->second;

    // give up if first unacked datagram was initially sent MAX_UNACKED_US ago
    const auto us_since_first_send = timestamp_us() - first_unacked.send_ts;

    if (us_since_first_send > MAX_UNACKED_US) {
      encode_flags = 1; // force next frame to be key frame

      cerr << "* Recovery: gave up retransmissions and forced a key frame "
           << frame_id_ << endl;

      if (verbose_) {
        cerr << "Giving up on lost datagram: frame_id="
             << first_unacked.frame_id << " frag_id=" << first_unacked.frag_id
             << " rtx=" << first_unacked.num_rtx
             << " us_since_first_send=" << us_since_first_send << endl;
      }

      // clean up
      send_buf_.clear();
      unacked_.clear();
    }
  }

  // encode a frame and calculate encoding time
  const auto encode_start = steady_clock::now();
  // 填充帧数据
  memcpy(frame_->data[0], raw_img.y_plane(),raw_img.y_size());  // Y plane
  memcpy(frame_->data[1], raw_img.u_plane(),raw_img.uv_size());  // U plane
  memcpy(frame_->data[2], raw_img.v_plane(),raw_img.uv_size());  // V plane

  if(encode_flags){
    frame_->pict_type = AV_PICTURE_TYPE_I;
  }else{
    frame_->pict_type = AV_PICTURE_TYPE_P;
  }
  frame_->pts = frame_id_;

  // 编码帧
  ret = avcodec_send_frame(codec_ctx_, frame_);
  if (ret < 0) {
      fprintf(stderr, "Error sending frame to encoder\n");
      return;
  }

  const auto encode_end = steady_clock::now();
  const double encode_time_ms = duration<double, milli>(
                                encode_end - encode_start).count();

  // track stats in the current period
  num_encoded_frames_++;
  total_encode_time_ms_ += encode_time_ms;
  max_encode_time_ms_ = max(max_encode_time_ms_, encode_time_ms);
}

size_t H264Encoder::packetize_encoded_frame()
{
  AVPacket *pkt = av_packet_alloc();
  int ret;
  unsigned int frames_encoded = 0;
  size_t frame_size = 0;

  while ((ret = avcodec_receive_packet(codec_ctx_, pkt))==0) {
    if (ret == 0) {
      frames_encoded++;
      // there should be exactly one frame encoded
      if (frames_encoded > 1) {
        throw runtime_error("Multiple frames were encoded at once");
      }

      frame_size = pkt->size;
      assert(frame_size > 0);

      // read the returned frame type
      auto frame_type = FrameType::NONKEY;
      if (frame_->pict_type == AV_PICTURE_TYPE_I) {
        frame_type = FrameType::KEY;
        if (verbose_) {
          cerr << "Encoded a key frame: frame_id=" << frame_id_ << endl;
        }
      }

      // total fragments to divide this frame into
      const uint16_t frag_cnt = narrow_cast<uint16_t>(
          frame_size / (Datagram::max_payload + 1) + 1);

      // next address to copy compressed frame data from
      uint8_t * buf_ptr = pkt->data;
      const uint8_t * const buf_end = buf_ptr + frame_size;

      for (uint16_t frag_id = 0; frag_id < frag_cnt; frag_id++) {
        // calculate payload size and construct the payload
        const size_t payload_size = (frag_id < frag_cnt - 1) ?
            Datagram::max_payload : buf_end - buf_ptr;

        // enqueue a datagram
        send_buf_.emplace_back(frame_id_, frame_type, frag_id, frag_cnt,
          string_view {reinterpret_cast<const char *>(buf_ptr), payload_size});

        buf_ptr += payload_size;
      }
    }
  }

  return frame_size;
}

void H264Encoder::add_unacked(const Datagram & datagram)
{
  const auto seq_num = make_pair(datagram.frame_id, datagram.frag_id);
  auto [it, success] = unacked_.emplace(seq_num, datagram);

  if (not success) {
    throw runtime_error("datagram already exists in unacked");
  }

  it->second.last_send_ts = it->second.send_ts;
}

void H264Encoder::add_unacked(Datagram && datagram)
{
  const auto seq_num = make_pair(datagram.frame_id, datagram.frag_id);
  auto [it, success] = unacked_.emplace(seq_num, move(datagram));

  if (not success) {
    throw runtime_error("datagram already exists in unacked");
  }

  it->second.last_send_ts = it->second.send_ts;
}

void H264Encoder::handle_ack(const shared_ptr<AckMsg> & ack)
{
  const auto curr_ts = timestamp_us();

  // observed an RTT sample
  add_rtt_sample(curr_ts - ack->send_ts);

  // find the acked datagram in 'unacked_'
  const auto acked_seq_num = make_pair(ack->frame_id, ack->frag_id);
  auto acked_it = unacked_.find(acked_seq_num);

  if (acked_it == unacked_.end()) {
    // do nothing else if ACK is not for an unacked datagram
    return;
  }

  // retransmit all unacked datagrams before the acked one (backward)
  for (auto rit = make_reverse_iterator(acked_it);
       rit != unacked_.rend(); rit++) {
    auto & datagram = rit->second;

    // skip if a datagram has been retransmitted MAX_NUM_RTX times
    if (datagram.num_rtx >= MAX_NUM_RTX) {
      continue;
    }

    // retransmit if it's the first RTX or the last RTX was about one RTT ago
    if (datagram.num_rtx == 0 or
        curr_ts - datagram.last_send_ts > ewma_rtt_us_.value()) {
      datagram.num_rtx++;
      datagram.last_send_ts = curr_ts;

      // retransmissions are more urgent
      send_buf_.emplace_front(datagram);
    }
  }

  // finally, erase the acked datagram from 'unacked_'
  unacked_.erase(acked_it);
}

void H264Encoder::add_rtt_sample(const unsigned int rtt_us)
{
  // min RTT
  if (not min_rtt_us_ or rtt_us < *min_rtt_us_) {
    min_rtt_us_ = rtt_us;
  }

  // EWMA RTT
  if (not ewma_rtt_us_) {
    ewma_rtt_us_ = rtt_us;
  } else {
    ewma_rtt_us_ = ALPHA * rtt_us + (1 - ALPHA) * (*ewma_rtt_us_);
  }
}

void H264Encoder::output_periodic_stats()
{
  cerr << "Frames encoded in the last ~1s: " << num_encoded_frames_ << endl;

  if (num_encoded_frames_ > 0) {
    cerr << "  - Avg/Max encoding time (ms): "
         << double_to_string(total_encode_time_ms_ / num_encoded_frames_)
         << "/" << double_to_string(max_encode_time_ms_) << endl;
  }

  if (min_rtt_us_ and ewma_rtt_us_) {
    cerr << "  - Min/EWMA RTT (ms): " << double_to_string(*min_rtt_us_ / 1000.0)
         << "/" << double_to_string(*ewma_rtt_us_ / 1000.0) << endl;
  }

  // reset all but RTT-related stats
  num_encoded_frames_ = 0;
  total_encode_time_ms_ = 0.0;
  max_encode_time_ms_ = 0.0;
}

void H264Encoder::set_target_bitrate(const unsigned int bitrate_kbps)
{
  target_bitrate_ = bitrate_kbps;

  codec_ctx_->bit_rate = target_bitrate_;
}
