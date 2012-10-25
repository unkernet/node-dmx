#ifndef DMX_H
#define DMX_H

#include <node.h>
#include "WinTypes.h"
#include "ftd2xx.h"

const char * ft_status[] = {
  "FT_OK",
  "FT_INVALID_HANDLE",
  "FT_DEVICE_NOT_FOUND",
  "FT_DEVICE_NOT_OPENED",
  "FT_IO_ERROR",
  "FT_INSUFFICIENT_RESOURCES",
  "FT_INVALID_PARAMETER",
  "FT_INVALID_BAUD_RATE",
  "FT_DEVICE_NOT_OPENED_FOR_ERASE",
  "FT_DEVICE_NOT_OPENED_FOR_WRITE",
  "FT_FAILED_TO_WRITE_DEVICE",
  "FT_EEPROM_READ_FAILED",
  "FT_EEPROM_WRITE_FAILED",
  "FT_EEPROM_ERASE_FAILED",
  "FT_EEPROM_NOT_PRESENT",
  "FT_EEPROM_NOT_PROGRAMMED",
  "FT_INVALID_ARGS",
  "FT_NOT_SUPPORTED",
  "FT_OTHER_ERROR"
};

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
  FT_HANDLE ftHandle;
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