AM_CPPFLAGS = $(CXX17_FLAGS) -I$(srcdir)/../util -I$(srcdir)/../video
AM_CXXFLAGS = $(PICKY_CXXFLAGS)
BASE_LDADD = ../video/libvideo.a ../util/libutil.a \
	$(VPX_LIBS) $(SDL_LIBS) -lpthread -lavcodec -lavformat -lavutil

bin_PROGRAMS = video_sender video_receiver

video_sender_SOURCES = video_sender.cc \
	protocol.hh protocol.cc h264encoder.hh h264encoder.cc
video_sender_LDADD = $(BASE_LDADD)

video_receiver_SOURCES = video_receiver.cc \
	protocol.hh protocol.cc h264decoder.hh h264decoder.cc
video_receiver_LDADD = $(BASE_LDADD)