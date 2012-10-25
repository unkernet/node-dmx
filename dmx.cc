#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <v8.h>
#include <node.h>
#include "WinTypes.h"
#include "ftd2xx.h"
#include "dmx.h"

#include <sys/time.h>
#include <time.h>

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
  FT_STATUS ftStatus;
  unsigned int numDevs;
  unsigned int i;
  
  ftStatus = FT_ListDevices(&numDevs, NULL, FT_LIST_NUMBER_ONLY);
  if (ftStatus != FT_OK)
    return scope.Close(ThrowException(Exception::Error(String::New("Can't enumerate ftdi2xx devices"))));
    
  Local<Array> ret = Array::New(numDevs);
  Local<Object> obj;
  
  if (numDevs > 0) {
    // Create buffers
    char **descr = new char * [numDevs + 1];
    char **serial = new char * [numDevs + 1];
    unsigned int *loc = new unsigned int [numDevs + 1];
    for (i = 0; i < numDevs; i++) {
      descr[i] = new char[64];
      serial[i] = new char[16];
    }
    descr[numDevs] = NULL;
    serial[numDevs] = NULL;
    
    // Enumerate devices
    ftStatus = (FT_ListDevices(descr,  &numDevs, FT_LIST_ALL | FT_OPEN_BY_DESCRIPTION  ) |
                FT_ListDevices(serial, &numDevs, FT_LIST_ALL | FT_OPEN_BY_SERIAL_NUMBER) |
                FT_ListDevices(loc,    &numDevs, FT_LIST_ALL | FT_OPEN_BY_LOCATION     ));        
    if (ftStatus == FT_OK)
      for (i = 0; i < numDevs; i++) {
        obj = Object::New();
        obj->Set(String::NewSymbol("description"), String::New(descr[i]));
        obj->Set(String::NewSymbol("serial"), String::New(serial[i]));
        obj->Set(String::NewSymbol("location"), Integer::New(loc[i]));
        ret->Set(i, obj);
      }
    
    // Free buffers
    for (i = 0; i < numDevs; i++) {
      delete[] descr[i];
      delete[] serial[i];
    }
    delete[] descr;
    delete[] serial;
    delete[] loc;
    
    if (ftStatus != FT_OK)
      return scope.Close(ThrowException(Exception::Error(String::New("Can't enumerate ftdi2xx devices"))));
  }
  return scope.Close(ret);
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
  FT_STATUS ftStatus;
  FT_HANDLE ftHandle;
  const size_t errln = 64;
  char err [errln];

  // Initalize new object
  DMX* obj = new DMX();
  pthread_mutex_init(&(obj->lock), NULL);
  obj->portOpen = obj->threadRun = obj->changes = false;
  obj->step=255;
  CalculateSleep(&(obj->sleep),25);
  memset(obj->dmxVal,0,512);
  memset(obj->newVal,0,512);
  
  // Open device
  unsigned int devid = args[0]->Int32Value();
  ftStatus = FT_Open(devid, &ftHandle);
  if (ftStatus != FT_OK) {
    snprintf(err, errln, "Can't open ftdi2xx device %d: %s", devid, ft_status[ftStatus]);
    return scope.Close(ThrowException(Exception::Error(String::New(err))));
  }
  
  // Setup device
  if ((FT_OK != (ftStatus = FT_SetBaudRate(ftHandle, 250000))) ||
      (FT_OK != (ftStatus = FT_SetDataCharacteristics(ftHandle, FT_BITS_8, FT_STOP_BITS_2, FT_PARITY_NONE))) ||
      (FT_OK != (ftStatus = FT_SetFlowControl(ftHandle, FT_FLOW_NONE, NULL, NULL))) ||
      (FT_OK != (ftStatus = FT_Purge(ftHandle, FT_PURGE_RX | FT_PURGE_TX)))) {
    snprintf(err, errln, "Can't setup device %d: %s", devid, ft_status[ftStatus]);
    return scope.Close(ThrowException(Exception::Error(String::New(err))));
  }
  
  obj->ftHandle = ftHandle;
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
  unsigned int bytesWritten;
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
    FT_SetBreakOn(obj->ftHandle);
    FT_SetBreakOff(obj->ftHandle);
    FT_Write(obj->ftHandle, &StartCode,  1,   &bytesWritten);
    FT_Write(obj->ftHandle, obj->dmxVal, 512, &bytesWritten);
    if (obj->sleep.tv_nsec>0) nanosleep (&(obj->sleep), NULL);
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