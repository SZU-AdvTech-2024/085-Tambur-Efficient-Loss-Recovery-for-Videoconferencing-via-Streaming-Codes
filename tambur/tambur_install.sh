#!/bin/bash
chmod +x autogen.sh; sudo apt-get update -y ; sudo apt-get install build-essential autoconf automake libtool -y ; sudo apt-get install libgtest-dev ; sudo apt-get install g++ -y ; sudo apt-get install pkg-config -y ; sudo apt-get install googletest -y ; sudo apt-get install libz-dev ; sudo apt-get install jq ; sudo apt-get install libjerasure2 ; sudo apt-get install -y protobuf-compiler libprotobuf-dev autotools-dev dh-autoreconf iptables pkg-config dnsmasq-base  apache2-bin debhelper libssl-dev ssl-cert libxcb-present-dev libcairo2-dev libpango1.0-dev apache2-dev libgtest-dev ; sudo apt install python3-pip ; pip3 install torch torchvision torchaudio ; pip3 install pandas ; pip3 install matplotlib ; cd ~/tambur/third_party/gf-complete-master ; chmod +x ./autogen.sh ; ./autogen.sh ; chmod +x configure ; ./configure ; make ; sudo make install ; cd ~/tambur/third_party/Jerasure-master/ ; autoreconf --force --install ; ./configure ; make -j2 ; sudo make install ; cd ~/tambur/third_party/ ; rm -r mahimahi-master; git clone https://github.com/ravinet/mahimahi ; cd mahimahi ; ./autogen.sh ; ./configure ; make -j2 ; sudo make install ; cd .. ; mv mahimahi mahimahi-master ; sudo add-apt-repository ppa:keithw/mahimahi ; sudo apt-get update ; sudo apt-get install mahimahi ; cd ~/tambur/third_party ; wget https://download.pytorch.org/libtorch/lts/1.8/cpu/libtorch-cxx11-abi-shared-with-deps-1.8.2%2Bcpu.zip ; unzip libtorch-cxx11-abi-shared-with-deps-1.8.2+cpu.zip ; rm libtorch-cxx11-abi-shared-with-deps-1.8.2+cpu.zip ;  cd ~/tambur/third_party ; git clone https://github.com/microsoft/ringmaster.git ; cd ringmaster ; sudo apt install autoconf libvpx-dev libsdl2-dev ; chmod +x autogen.sh ; ./autogen.sh ; ./configure ; make -j ; cd  ~/tambur ; chmod +x autogen.sh ; ./autogen.sh ; ./configure ; make -j ; 
