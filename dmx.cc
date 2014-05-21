#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <v8.h>
#include <node.h>
#include <ftdi.h>
#include <sys/time.h>
#include <time.h>
#include "dmx.h"

using namespace v8;

int min(int a,int b) {
  return ( a < b ? a : b);
}
int cmp(int a,int b) {
  return ( a == b ? 0 : ( a > b ? 1 : -1 ));
}
void CalculateSleep (timespec *sleep, unsigned int Hz) {
  const long transmitTime = 21000000;
  long ns = 1000000000 / Hz - transmitTime;
  if (ns < 0) ns = 0;
  sleep->tv_sec = 0;
  sleep->tv_nsec = ns;
}

Handle<Value> list(const Arguments& args) {
  HandleScope scope;

  unsigned int i;
  int ret;
  struct ftdi_context ftdic;
  struct ftdi_device_list *devlist, *curdev;
  char manufacturer[128], description[128], serial[128];
  const size_t errln = 128;
  char err[errln];

  if (ftdi_init(&ftdic) < 0) {
    return scope.Close(ThrowException(Exception::Error(String::New("ftdi_init failed"))));
  }
  if ((ret = ftdi_usb_find_all(&ftdic, &devlist, 0x0403, 0x6001)) < 0) {
    snprintf(err, errln, "ftdi_usb_find_all failed: %d (%s)", ret, ftdi_get_error_string(&ftdic));
    return scope.Close(ThrowException(Exception::Error(String::New(err))));
  }
  Local<Array> result = Array::New(ret);
  Local<Object> obj;

  i = 0;
  for (curdev = devlist; curdev != NULL; i++) {
      if ((ret = ftdi_usb_get_strings(&ftdic, curdev->dev, manufacturer, 128, description, 128, serial, 128)) < 0) {
        snprintf(err, errln, "ftdi_usb_get_strings failed: %d (%s)", ret, ftdi_get_error_string(&ftdic));
        scope.Close(ThrowException(Exception::Error(String::New(err))));
      }
      obj = Object::New();
      obj->Set(String::NewSymbol("manufacturer"), String::New(manufacturer));
      obj->Set(String::NewSymbol("description"), String::New(description));
      obj->Set(String::NewSymbol("serial"), String::New(serial));
      result->Set(i, obj);
      curdev = curdev->next;
  }

  ftdi_list_free(&devlist);
  ftdi_deinit(&ftdic);

  return scope.Close(result);
}

Handle<Value> newDMX(const Arguments& args) {
  HandleScope scope;
  return scope.Close(DMX::NewInstance(args));
}

DMX::DMX() {};
DMX::~DMX() {};
Persistent<Function> DMX::constructor;

void DMX::Init() {
  Local<FunctionTemplate> tpl = FunctionTemplate::New(New);
  tpl->SetClassName(String::NewSymbol("DMX"));
  tpl->InstanceTemplate()->SetInternalFieldCount(1);
  NODE_SET_PROTOTYPE_METHOD(tpl, "start", Start);
  NODE_SET_PROTOTYPE_METHOD(tpl, "stop", Stop);
  NODE_SET_PROTOTYPE_METHOD(tpl, "set", Set);
  NODE_SET_PROTOTYPE_METHOD(tpl, "step", Step);
  NODE_SET_PROTOTYPE_METHOD(tpl, "setHz", SetHz);
  constructor = Persistent<Function>::New(tpl->GetFunction());
}

Handle<Value> DMX::New(const Arguments& args) {
  HandleScope scope;
  
  // Initalize new object
  DMX* obj = new DMX();
  pthread_mutex_init(&(obj->lock), NULL);
  obj->portOpen = obj->threadRun = obj->changes = false;
  obj->step=255;
  CalculateSleep(&(obj->sleep),25);
  memset(obj->dmxVal,0,512);
  memset(obj->newVal,0,512);
  
  struct ftdi_context ftdic;
  struct ftdi_device_list *devlist, *curdev;
  const size_t errln = 128;
  char err[errln];
  int ret;
  unsigned int i = 0, devid = args[0]->Int32Value();

  // Find device
  if (ftdi_init(&ftdic) < 0)
    return scope.Close(ThrowException(Exception::Error(String::New("ftdi_init failed"))));
  if ((ret = ftdi_usb_find_all(&ftdic, &devlist, 0x0403, 0x6001)) < 0) {
    snprintf(err, errln, "ftdi_usb_find_all failed: %d (%s)", ret, ftdi_get_error_string(&ftdic));
    ftdi_list_free(&devlist);
    ftdi_deinit(&ftdic);
    return scope.Close(ThrowException(Exception::Error(String::New(err))));
  }
  for (curdev = devlist; curdev != NULL; i++) {
    if (i == devid) break;
    curdev = curdev->next;
  }
  if (curdev == NULL) {
    snprintf(err, errln, "Cannot find device #%d", devid);
    ftdi_list_free(&devlist);
    ftdi_deinit(&ftdic);
    return scope.Close(ThrowException(Exception::Error(String::New(err))));
  }

  // Open device
  if ((ret = ftdi_usb_open_dev(&ftdic, curdev->dev)) < 0) {
    snprintf(err, errln, "ftdi_usb_open_dev failed: %d (%s)", ret, ftdi_get_error_string(&ftdic));
    ftdi_list_free(&devlist);
    ftdi_deinit(&ftdic);
    return scope.Close(ThrowException(Exception::Error(String::New(err))));
  }
  ftdi_list_free(&devlist);

  // Setup device
  if (((ret = ftdi_set_baudrate(&ftdic, 250000)) < 0) || 
      ((ret = ftdi_set_line_property2(&ftdic, BITS_8, STOP_BIT_2, NONE, BREAK_ON)) < 0) ||
      ((ret = ftdi_usb_purge_buffers(&ftdic)) < 0)) {
    snprintf(err, errln, "Can't setup device: %d (%s)", ret, ftdi_get_error_string(&ftdic));
    ftdi_deinit(&ftdic);
    return scope.Close(ThrowException(Exception::Error(String::New(err))));
  }

  obj->ftdic = ftdic;
  obj->portOpen = true;
  obj->Wrap(args.This());
  return args.This();
}

Handle<Value> DMX::NewInstance(const Arguments& args) {
  HandleScope scope;
  Handle<Value> argv[1] = { args[0] };
  Local<Object> instance = constructor->NewInstance(1, argv);
  return scope.Close(instance);
}

void *DMX::thread_func(void* arg) {
  DMX *obj = (DMX*) arg;
  int i;
  unsigned char StartCode = 0;
  obj->threadRun = true;

  while (obj->threadRun) {
    pthread_mutex_lock(&(obj->lock));
    // Check for the changes flag
    if (obj->changes) {
      obj->changes = false;
      for (i = 0; i < 512; i++) {
        // Change level for one step and put flag back it is not enough
        obj->dmxVal[i] += cmp(obj->newVal[i] ,obj->dmxVal[i]) * min(abs(obj->dmxVal[i] - obj->newVal[i]), obj->step);
        if (obj->dmxVal[i] != obj->newVal[i]) obj->changes = true;
      }
    }
    pthread_mutex_unlock(&(obj->lock));

    // Transmit DMX packet
    ftdi_set_line_property2(&obj->ftdic, BITS_8, STOP_BIT_2, NONE, BREAK_ON);
    ftdi_set_line_property2(&obj->ftdic, BITS_8, STOP_BIT_2, NONE, BREAK_OFF);
    ftdi_write_data(&obj->ftdic, &StartCode,  1);
    ftdi_write_data(&obj->ftdic, obj->dmxVal, 512);
    if (obj->sleep.tv_nsec > 0) nanosleep(&(obj->sleep), NULL);
  }
  
  return NULL;
}

Handle<Value> DMX::Start(const Arguments& args) {
  HandleScope scope;
  DMX *obj = ObjectWrap::Unwrap<DMX>(args.This());
  
  // Start working thread
  if (!obj->portOpen ||
      obj->threadRun ||
      (pthread_create(&(obj->thread), NULL, obj->thread_func, obj) != 0)
     )
    return scope.Close(Boolean::New(false));
    
  return scope.Close(Boolean::New(true));
}

Handle<Value> DMX::Stop(const Arguments& args) {
  HandleScope scope;
  DMX *obj = ObjectWrap::Unwrap<DMX>(args.This());
  
  if (!obj->threadRun || !obj->portOpen)
    return scope.Close(Boolean::New(false));
  
  obj->threadRun = false;

  // Wait while thread ends if we want
  if (args[0]->BooleanValue()) {
    pthread_join(obj->thread, NULL);
  }

  return scope.Close(Boolean::New(true));
}

Handle<Value> DMX::Step(const Arguments& args) {
  HandleScope scope;
  DMX *obj = ObjectWrap::Unwrap<DMX>(args.This());
  
  int s = args[0]->Int32Value();
  if (s < 1 || s > 255) s=255;
  
  obj->step = s;
  return scope.Close(Boolean::New(true));
}

Handle<Value> DMX::SetHz(const Arguments& args) {
  HandleScope scope;
  DMX *obj = ObjectWrap::Unwrap<DMX>(args.This());

  int Hz=args[0]->Int32Value();
  if (Hz < 1 || Hz > 50) return scope.Close(Boolean::New(false));
  CalculateSleep(&(obj->sleep),Hz);

  return scope.Close(Boolean::New(true));
}

Handle<Value> DMX::Set(const Arguments& args) {
  HandleScope scope;
  DMX *obj = ObjectWrap::Unwrap<DMX>(args.This());
  int i, l, val;
  bool setAll;
  Local<Array> arr;
  
  // Start the working thread if it is not
  if (!obj->threadRun) Start(args);
  
  // Set all channels to the same level if argument in not an Array
  if (args[0]->IsArray()) {
    setAll=false;
    arr = Local<Array>::Cast(args[0]);
    l = arr->Length(); 
    if (l > 512) l = 512;
  } else {
    setAll = true;
    val=args[0]->Int32Value();
    l = 512;
    if (val < 0) val = 0;
    if (val > 255) val = 255;
  }
  
  pthread_mutex_lock(&(obj->lock));
  for (i = 0; i < l; i++) {
    if (!setAll) {
      val = arr->Get(i)->Int32Value();
      if (val < 0) val = 0;
      if (val > 255) val = 255;
    }
    
    // Check for the changes
    if (obj->newVal[i] != val) {
      obj->newVal[i] = val;
      obj->changes = true;
    }
  }
  pthread_mutex_unlock(&(obj->lock));
  
  // sum is a number of non-zero channels
  int sum = 0;
  for (i = 0; i < 512; i++)
    sum += obj->newVal[i] > 0 ? 1 : 0;
    
  return scope.Close(Integer::New(sum));
}

void Init (Handle<Object> target) {
  DMX::Init();
  NODE_SET_METHOD(target, "list", list);
  NODE_SET_METHOD(target, "DMX", newDMX);
}

NODE_MODULE(dmx_native, Init)