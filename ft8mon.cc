//
// decode FT8 from a sound card
//
// Robert Morris, AB1HL
//

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <vector>
#include <time.h>
#include <string.h>
#include <mutex>
#include <map>
#include <string>
#include <thread>
#include "snd.h"
#include "util.h"
#include "unpack.h"
#include "ft8.h"
#include "fft.h"
#ifdef USE_HPSDR
#include "hpsdr.h"
#endif

std::mutex cycle_mu;
volatile int cycle_count;
time_t saved_cycle_start;
std::map<std::string,bool> cycle_already;

//
// a91 is 91 bits -- 77 plus the 14-bit CRC.
//
int
hcb(int *a91, double hz0, double hz1, double off,
    const char *comment, double snr, int pass,
    int correct_bits)
{
  std::string msg = unpack(a91);

  cycle_mu.lock();

  if(cycle_already.count(msg) > 0){
    // already decoded this message on this cycle
    cycle_mu.unlock();
    return 1; // 1 => already seen, don't subtract.
  }

  cycle_already[msg] = true;
  cycle_count += 1;

  cycle_mu.unlock();

  struct tm result;
  gmtime_r(&saved_cycle_start, &result);

  printf("%02d%02d%02d %3d %3d %5.2f %6.1f %s\n",
         result.tm_hour,
         result.tm_min,
         result.tm_sec,
         (int)snr,
         correct_bits,
         off - 0.5,
         hz0,
         msg.c_str());
  fflush(stdout);
  
  return 2; // 2 => new decode, do subtract.
}

void
usage()
{
  fprintf(stderr, "Usage: ft8mon -card card channel\n");
#ifdef USE_AIRSPYHF
  fprintf(stderr, "       ft8mon -card airspy serial,mhz\n");
#endif
  fprintf(stderr, "       ft8mon -levels card channel\n");
  fprintf(stderr, "       ft8mon -list\n");
  fprintf(stderr, "       ft8mon -file xxx.wav ...\n");
  exit(1);
}

#ifdef USE_RTLSDR
double RTLSDRSoundIn::time_ = -1;
rtlsdr_dev_t *  RTLSDRSoundIn::rtl_device = 0;
std::thread * RTLSDRSoundIn::dongleThread;
std::mutex RTLSDRSoundIn::dataLock;
uint32_t RTLSDRSoundIn::signalBufferIndex = 0;
int8_t RTLSDRSoundIn::signal_r[RTLSDRSoundIn::SIGNAL_BUFFER_SIZE];
#endif

bool stayInProgram = true;

void sigint_callback_handler(int signum) {
  fprintf(stderr, "caught signal %d\n", signum);
  stayInProgram = false;
}


int
main(int argc, char *argv[])
{
  int hints[2] = { 2, 0 }; // CQ
  double budget = 5; // compute for this many seconds per cycle

  extern int fftw_type;
  fftw_type = FFTW_ESTIMATE; // rather than FFTW_MEASURE

  extern int nthreads;
  nthreads = 4; // multi-core

  signal(SIGINT, &sigint_callback_handler);
  signal(SIGTERM, &sigint_callback_handler);
  signal(SIGILL, &sigint_callback_handler);
  signal(SIGFPE, &sigint_callback_handler);
  signal(SIGSEGV, &sigint_callback_handler);
  signal(SIGABRT, &sigint_callback_handler);
  
  
  if(argc == 4 && strcmp(argv[1], "-card") == 0){
    int wanted_rate = 12000;
    SoundIn *sin = SoundIn::open(argv[2], argv[3], wanted_rate);
    sin->start();
    int rate = sin->rate();
    fprintf(stderr, "rate in main program: %d\n",rate);

    while(stayInProgram){
      // sleep until 14 seconds into the next 15-second cycle.
      double tt = now();
      long long cycle_start = tt - ((long long)tt % 15);

      if(tt - cycle_start >= 14){
        double ttt_start;
        // asking for no more than 15 seconds of samples in order
        // to avoid missing in fftw plan cache.
        // the "1" asks for the most recent 15 seconds of samples,
        // not the oldest buffered. it causes samples before the
        // most recent 15 seconds to be discarded.
        std::vector<double> samples = sin->get(15 * rate, ttt_start, 1);
        fprintf(stderr, "get took about %5.2f seconds\n", now() - tt);

        // ttt_start is UNIX time of samples[0].
        double ttt_end = ttt_start + samples.size() / rate;
        cycle_start = ((long long) (ttt_end / 15)) * 15;

        // sample # of 0.5 seconds into the 15-second cycle.
        long long nominal_start = samples.size() - rate * (ttt_end - cycle_start - 0.5);

        if(nominal_start >= 0 && nominal_start + 10*rate < (int) samples.size()){
          struct tm result;
          time_t tx = cycle_start;
          gmtime_r(&tx, &result);

          // make samples exactly 15 seconds, to make
          // fftw plan caching more effective.
          samples.resize(15 * rate, 0.0);

          cycle_mu.lock();
          cycle_count = 0;
          saved_cycle_start = cycle_start; // for hcb() callback
          cycle_already.clear();
          cycle_mu.unlock();

          double s = now();
          entry(samples.data(), samples.size(), nominal_start, rate,
                150,
                3600, // 2900,
                hints, hints, budget, budget, hcb,
                0, (struct cdecode *) 0);
          printf("%02d:%02d:%02d decodes: %d, processing time %3.1f seconds\n",
                 result.tm_hour,
                 result.tm_min,
                 result.tm_sec,
                 cycle_count, now() - s);
        } else {
          fprintf(stderr, "didn't try to decode\n");
        }

        sleep(2);
      }
      usleep(100 * 1000); // 0.1 seconds
    }
    delete sin;
    fprintf(stderr, "Sound input device has been shut down\n");
  } else if(argc == 4 && strcmp(argv[1], "-levels") == 0){
    SoundIn *sin = SoundIn::open(argv[2], argv[3], 12000);
    sin->start();
    sin->levels();
  } else if(argc >= 3 && strcmp(argv[1], "-file") == 0){
    for(int ii = 2; ii < argc; ii++){
      // the .wav file should start at an even 15-second boundary.
      int rate;
      std::vector<double> s = readwav(argv[ii], rate);
      entry(s.data(), s.size(), 0.5 * rate, rate,
            150,
            3600, // 2900,
            hints, hints, budget, budget, hcb,
            0, (struct cdecode *) 0);
    }
    extern void fft_stats();
    // fft_stats();
  } else if(argc == 2 && strcmp(argv[1], "-list") == 0){
    snd_list();
  } else {
    usage();
  }
}
