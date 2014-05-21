#ifndef DMX_H
#define DMX_H

#include <node.h>
#include <ftdi.h>



class DMX : public node::ObjectWrap {
 public:
  static void Init();
  static v8::Handle<v8::Value> NewInstance(const v8::Arguments& args);

 private:
  DMX();
  ~DMX();

  static v8::Persistent<v8::Function> constructor;
  static v8::Handle<v8::Value> New(const v8::Arguments& args);
  static v8::Handle<v8::Value> Start(const v8::Arguments& args);
  static v8::Handle<v8::Value> Stop(const v8::Arguments& args);
  static v8::Handle<v8::Value> Set(const v8::Arguments& args);
  static v8::Handle<v8::Value> Step(const v8::Arguments& args);
  static v8::Handle<v8::Value> SetHz(const v8::Arguments& args);
  static void *thread_func(void* arg);
  struct ftdi_context ftdic;
  pthread_t thread;
  pthread_mutex_t lock;
  unsigned char step;
  bool portOpen;
  bool threadRun;
  bool changes;
  unsigned char dmxVal[512];
  unsigned char newVal[512];
  struct timespec sleep;
};

#endif