/**
 * Master thesis
 * by Alf-Andre Walla 2016-2017
 * 
**/
#include <service>
#include <net/inet4>
#include <profile>
#include <util/crc32.hpp>
#include <cstdio>
#include "liveupdate.hpp"
#include "hw_timer.hpp"

#include <kernel/os.hpp>
extern uintptr_t heap_begin;
extern uintptr_t heap_end;
void show_heap_stats()
{
  uintptr_t heap_total = OS::heap_max() - heap_begin;
  double total = (heap_end - heap_begin) / (double) heap_total;
  
  fprintf(stderr, "\tHeap is at: %#x / %#x  (diff=%#x)\n",
         heap_end, OS::heap_max(), OS::heap_max() - heap_end);
  fprintf(stderr, "\tHeap usage: %u / %u Kb (%.2f%%)\n",
         (heap_end - heap_begin) / 1024, 
         heap_total / 1024,
         total * 100.0);
}

static void* LIVEUPD_LOCATION   = (void*) 0x1100000; // at 20mb
static const uint16_t TERM_PORT = 6667;

typedef net::tcp::Connection_ptr Connection_ptr;
static std::vector<Connection_ptr> saveme;
static std::vector<std::string>    savemsg;

void setup_terminal_connection(Connection_ptr conn)
{
  saveme.push_back(conn);
  // retrieve binary
  conn->on_read(512,
  [conn] (net::tcp::buffer_t buf, size_t n)
  {
    std::string str((const char*) buf.get(), n);
    printf("Received message %u: %s", savemsg.size()+1, str.c_str());
    // save for later as strings
    savemsg.push_back(str);
  });
  conn->on_close(
  [conn] {
    printf("Terminal %s closed\n", conn->to_string().c_str());
  });
}

template <typename T>
void setup_terminal(T& inet)
{
  // mini terminal
  printf("Setting up terminal on port %u\n", TERM_PORT);
  
  auto& term = inet.tcp().bind(TERM_PORT);
  term.on_connect(
  [] (auto conn) {
    setup_terminal_connection(conn);
    // write a string to change the state
    char BUFFER_CHAR = 'A';
    static uint32_t crc = CRC32_BEGIN();
    static const int LEN = 4096;
    auto buf = net::tcp::buffer_t(new uint8_t[LEN], std::default_delete<uint8_t[]>());
    
    for (int i = 0; i < 1000; i++) {
      memset(buf.get(), BUFFER_CHAR, LEN);
      conn->write(buf, LEN,
      [conn, buf] (int) {
        
        crc = crc32(crc, (char*) buf.get(), LEN);
        printf("[%p] CRC32: %08x   %s\n", buf.get(), CRC32_VALUE(crc), conn->to_string().c_str());
      });
      
      //BUFFER_CHAR++;
      if (BUFFER_CHAR > 'Z') BUFFER_CHAR = 'A';
    }
  });
}

template <typename T>
void setup_liveupdate_server(T& inet);

void Service::start()
{
  OS::add_stdout_default_serial();
  printf("Heap stats for Service::start():\n");
  show_heap_stats();
}
void Service::ready()
{
  printf("Heap stats for Service::ready():\n");
  show_heap_stats();
  volatile HW_timer timer("Service::ready()");
  printf("\n");
  printf("-= Starting LiveUpdate test service =-\n");

  auto& inet = net::Inet4::ifconfig<0>(
        { 10,0,0,42 },     // IP
        { 255,255,255,0 }, // Netmask
        { 10,0,0,1 },      // Gateway
        { 10,0,0,1 });     // DNS

  /// attempt to resume (if there is anything to resume)
  using namespace liu;
  void strings_and_buffers(Restore&);
  void the_timing(Restore&);
  void restore_term(Restore&);
  void saved_message(Restore&);
  void on_update_area(Restore&);
  void on_missing(Restore&);

  LiveUpdate::on_resume(0,   strings_and_buffers);
  LiveUpdate::on_resume(100, the_timing);
  LiveUpdate::on_resume(665, saved_message);
  LiveUpdate::on_resume(666, restore_term);
  LiveUpdate::on_resume(999, on_update_area);
  // begin restoring saved data
  if (LiveUpdate::resume(LIVEUPD_LOCATION, on_missing) == false) {
    printf("* Not restoring data, because no update has happened\n");
    // .. logic for when there is nothing to resume yet
  }
  setup_liveupdate_server(inet);
  
  // listen for telnet clients
  setup_terminal(inet);
  // show profile stats for boot
  printf("%s\n", ScopedProfiler::get_statistics().c_str());

  printf("Heap starts for END OF START:\n");
  show_heap_stats();
}

static std::vector<double> timestamps;

#include <hw/cpu.hpp>
void save_stuff(liu::Storage& storage, liu::buffer_len final_blob)
{
  storage.add_int(0, 1234);
  storage.add_int(0, 5678);

  storage.add_string(1, "Some string :(");
  storage.add_string(1, "Some other string :(");

  char buffer[] = "Just some random buffer";
  storage.add_buffer(1, {buffer, sizeof(buffer)});

  std::vector<std::string> strvec;
  strvec.push_back("|String 1|");
  strvec.push_back("|String 2 is slightly longer|");
  storage.add_vector<std::string> (1, strvec);

  // store current timestamp using same ID = 100
  int64_t ts = hw::CPU::rdtsc();
  storage.add<int64_t>(100, ts);
  printf("! CPU ticks before: %lld\n", ts);

  // store vector of timestamps
  storage.add_vector<double> (100, timestamps);

  // where the update was stored last
  printf("Storing location %p:%d\n", final_blob.buffer, final_blob.length);
  storage.add_buffer(999, final_blob.buffer, final_blob.length);
  
  // messages received from terminals
  storage.add_vector<std::string> (665, savemsg);

  // open terminals
  for (auto conn : saveme)
    if (conn->is_connected())
      storage.add_connection(666, conn);
}

void strings_and_buffers(liu::Restore& thing)
{
  int v1 = thing.as_int();      thing.go_next();
  printf("[int] has value [%d]\n", v1);
  assert(v1 == 1234);

  int v2 = thing.as_int();      thing.go_next();
  printf("[int] has value [%d]\n", v2);
  assert(v2 == 5678);

  auto str = thing.as_string(); thing.go_next();
  printf("[string] has value [%s]\n", str.c_str());
  assert(str == "Some string :(");

  str = thing.as_string();      thing.go_next();
  printf("[string] has value [%s]\n", str.c_str());
  assert(str == "Some other string :(");

  auto buffer = thing.as_buffer(); thing.go_next();
  printf("[buffer] is %d bytes long\n", thing.length());
  printf("As text: %.*s\n", buffer.length, buffer.buffer);
  // there is an extra zero at the end of the buffer
  str = std::string(buffer.buffer, buffer.length-1);
  assert(str == "Just some random buffer");

  auto vec = thing.as_vector<std::string> (); thing.go_next();
  printf("[strvec] Count: %u\n", vec.size());
  for (auto& str : vec) {
    printf("[strvec] len=%u str=%s\n", str.size(), str.c_str());
  }
  assert(vec[0] == "|String 1|");
  assert(vec[1] == "|String 2 is slightly longer|");
}
void saved_message(liu::Restore& thing)
{
  auto vec = thing.as_vector<std::string> ();
  for (auto& str : vec)
  {
    static int n = 0;
    printf("[%d] %s", ++n, str.c_str());
    // re-save it
    //savemsg.push_back(str);
  }
}
void on_missing(liu::Restore& thing)
{
  printf("Missing resume function for %u\n", thing.get_id());
}

void the_timing(liu::Restore& thing)
{
  auto t1 = thing.as_type<int64_t>();
  auto t2 = hw::CPU::rdtsc();
  printf("! CPU ticks after: %lld  (CPU freq: %f)\n", t2, OS::cpu_freq().count());

  using namespace std::chrono;
  double  div  = OS::cpu_freq().count() * 1000.0;
  double  time = (t2-t1) / div;

  char buffer[256];
  int len = snprintf(buffer, sizeof(buffer),
             "! Boot time in ticks: %lld (%.2f ms)\n", t2-t1, time);

  savemsg.emplace_back(buffer, len);
  // verify that the next id is still same as current
  assert(thing.next_id() == thing.get_id());
  // next thing with pre-update timestamp
  thing.go_next();

  // restore timestamp vector
  timestamps = thing.as_vector<double> ();  thing.go_next();
  // add new update time
  timestamps.push_back(time);
  // calculate average boot time over many updates
  double average = 0.0;
  for (double d : timestamps) average += d; average /= timestamps.size();

  printf("Restored %u timestamps, average TS: %.2f ms\n",
      timestamps.size(), average);
  
}
void restore_term(liu::Restore& thing)
{
  auto& stack = net::Inet4::stack<0> ();
  // restore connection to terminal
  auto conn = thing.as_tcp_connection(stack.tcp());
  setup_terminal_connection(conn);
  printf("Restored terminal connection to %s\n", conn->remote().to_string().c_str());
  
  // send all the messages so far
  //for (auto msg : savemsg)
  //  conn->write(msg);
}

#include <timers>
extern "C"
void _print_elf_symbols();
void on_update_area(liu::Restore& thing)
{
  auto updloc = thing.as_buffer().deep_copy();
  printf("Reloading from %p:%d\n", updloc.buffer, updloc.length);
  
  // we are perpetually updating ourselves
  using namespace std::chrono;
  Timers::oneshot(milliseconds(2500),
  [updloc] (auto) {
    extern uintptr_t heap_end;
    printf("* Re-running previous update at %p vs heap %#x\n", updloc.buffer, heap_end);
    liu::LiveUpdate::begin(LIVEUPD_LOCATION, updloc, save_stuff);
  });
}

#include <stdexcept>
template <typename T>
void setup_liveupdate_server(T& inet)
{
  // listen for live updates
  auto& server = inet.tcp().bind(666);
  server.on_connect(
  [] (auto conn)
  {
    printf("Receiving liveupdate blob\n");
    show_heap_stats();

    static const int UPDATE_MAX = 1024*1024 * 2; // 2mb files supported
    char* update_blob = new char[UPDATE_MAX];
    int*  update_size = new int(0);

    // retrieve binary
    conn->on_read(9000,
    [conn, update_blob, update_size] (net::tcp::buffer_t buf, size_t n)
    {
      if (*update_size + n > UPDATE_MAX) return;
      memcpy(update_blob + *update_size, buf.get(), n);
      *update_size += (int) n;

    }).on_close(
    [update_blob, update_size] {
      // we received a binary:
      float frac = *update_size / (float) UPDATE_MAX * 100.f;
      printf("* New update size: %u b  (%.2f%%) stored at %p\n", *update_size, frac, update_blob);
      try
      {
        // run live update process
        liu::LiveUpdate::begin(LIVEUPD_LOCATION, {update_blob, *update_size}, save_stuff);
      }
      catch (std::exception& err)
      {
        printf("Live Update location: %p\n", LIVEUPD_LOCATION);
        show_heap_stats();
        printf("Live update failed:\n%s\n", err.what());
      }
      
      delete[] update_blob;
    });
  });
}
