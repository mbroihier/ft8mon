CXX = clang++ -O
# CXX += -g -fsanitize=address
# CXX = g++9 -O3
FLAGS = -std=c++17 -I/opt/local/include -I/usr/local/include -I/opt/local/include/libairspyhf
LIBS = -L/opt/local/lib -L/usr/local/lib -lfftw3 -lsndfile

MOREC = 
MOREH = 

# uncomment if you have the airspyhf and liquid dsp libraries.
# CXX += -DUSE_AIRSPYHF
# LIBS += -lairspyhf -lliquid -lusb

# CXX += -DUSE_HPSDR
# MOREC += hpsdr.cc
# MOREH += hpsdr.h
# LIBS += -lliquid

# CXX += -DUSE_SDRIP
# MOREC += sdrip.cc
# MOREH += sdrip.h
# LIBS += -lliquid

ft8mon: ft8.cc ft8mon.cc snd.cc libldpc.c osd.cc unpack.cc util.cc fft.cc cloudsdr.h cloudsdr.cc $(MOREC) $(MOREH)
	$(CXX) $(FLAGS) ft8mon.cc ft8.cc unpack.cc osd.cc snd.cc util.cc fft.cc libldpc.c cloudsdr.cc $(MOREC) -o ft8mon $(LIBS) -lportaudio -pthread

clean:
	rm -f ft8mon
