// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

/* BrowserX mobile — minimal jni.h stand-in used to *syntax-check* the JNI bridge
 * on machines without an Android NDK (the desktop `make check` runs it).
 *
 * This is not a JNI implementation and not a redistribution of the NDK headers:
 * it declares exactly the types and call-table entries core_bridge.cc uses, so a
 * typo like `IntOf(object, "x", 0)` on a wrong field or a missing include is
 * caught here. On a real Android build the NDK's own jni.h is used instead —
 * the include path below is only consulted when the NDK is absent.
 */

#ifndef BROWSERX_MOBILE_CHECK_JNI_H_
#define BROWSERX_MOBILE_CHECK_JNI_H_

#include <cstdarg>
#include <cstddef>
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char jboolean;
typedef signed char jbyte;
typedef unsigned short jchar;
typedef short jshort;
typedef int jint;
typedef long long jlong;
typedef float jfloat;
typedef double jdouble;
typedef jint jsize;

#ifdef __cplusplus
class _jobject {};
class _jclass : public _jobject {};
class _jstring : public _jobject {};
class _jarray : public _jobject {};
class _jobjectArray : public _jarray {};
class _jthrowable : public _jobject {};
typedef _jobject* jobject;
typedef _jclass* jclass;
typedef _jstring* jstring;
typedef _jarray* jarray;
typedef _jobjectArray* jobjectArray;
typedef _jthrowable* jthrowable;
#else
typedef void* jobject;
typedef jobject jclass;
typedef jobject jstring;
typedef jobject jarray;
typedef jobject jobjectArray;
typedef jobject jthrowable;
#endif

typedef union jvalue {
  jboolean z;
  jbyte b;
  jchar c;
  jshort s;
  jint i;
  jlong j;
  jfloat f;
  jdouble d;
  jobject l;
} jvalue;

typedef struct _jmethodID* jmethodID;
typedef struct _jfieldID* jfieldID;

struct JNIEnv;
struct JavaVM;

struct JNINativeInterface {
  void* reserved0;
  void* reserved1;
  void* reserved2;
  void* reserved3;
  jint (*GetVersion)(JNIEnv*);
  jclass (*FindClass)(JNIEnv*, const char*);
  void* FromReflectedMethod;
  void* FromReflectedField;
  void* ToReflectedMethod;
  jclass (*GetSuperclass)(JNIEnv*, jclass);
  jboolean (*IsAssignableFrom)(JNIEnv*, jclass, jclass);
  void* ToReflectedField;
  void* Throw;
  void* ThrowNew;
  void* ExceptionOccurred;
  void* ExceptionDescribe;
  void* ExceptionClear;
  void* FatalError;
  void* reserved5;
  void* reserved6;
  void* reserved7;
  void* reserved8;
  void* reserved9;
  void* reserved10;
  void* reserved11;
  void* reserved12;
  void* reserved13;
  void* reserved14;
  void* reserved15;
  void* reserved16;
  void* reserved17;
  void* reserved18;
  void* reserved19;
  void* reserved20;
  void* reserved21;
  void* reserved22;
  void* reserved23;
  void* reserved24;
  void* reserved25;
  jobjectArray (*NewObjectArray)(JNIEnv*, jsize, jclass, jobject);
  jobject (*GetObjectArrayElement)(JNIEnv*, jobjectArray, jsize);
  void (*SetObjectArrayElement)(JNIEnv*, jobjectArray, jsize, jobject);
  void* reserved26;
  const char* (*GetStringUTFChars)(JNIEnv*, jstring, jboolean*);
  jsize (*GetStringUTFLength)(JNIEnv*, jstring);
  void (*ReleaseStringUTFChars)(JNIEnv*, jstring, const char*);
  jstring (*NewStringUTF)(JNIEnv*, const char*);
  jsize (*GetArrayLength)(JNIEnv*, jarray);
  void* reserved27;
  void* reserved28;
  void* reserved29;
  void* reserved30;
  void* reserved31;
  void* reserved32;
  void* reserved33;
};

struct JNIEnv {
  const JNINativeInterface* functions;

#ifdef __cplusplus
  jclass FindClass(const char* name) { return functions->FindClass(this, name); }
  jobjectArray NewObjectArray(jsize length, jclass element, jobject initial) {
    return functions->NewObjectArray(this, length, element, initial);
  }
  void SetObjectArrayElement(jobjectArray array, jsize index, jobject value) {
    functions->SetObjectArrayElement(this, array, index, value);
  }
  const char* GetStringUTFChars(jstring value, jboolean* is_copy) {
    return functions->GetStringUTFChars(this, value, is_copy);
  }
  void ReleaseStringUTFChars(jstring value, const char* chars) {
    functions->ReleaseStringUTFChars(this, value, chars);
  }
  jstring NewStringUTF(const char* chars) { return functions->NewStringUTF(this, chars); }
  jsize GetArrayLength(jarray array) { return functions->GetArrayLength(this, array); }
#endif
};

struct JNINativeInterface_;

#ifdef __cplusplus
}  // extern "C"
#endif

/* Matches Android's jni.h: exported entry points must survive
 * -fvisibility=hidden, which the mobile build uses to keep the core's internals
 * out of the .so's dynamic symbol table. */
#define JNIEXPORT __attribute__((visibility("default")))
#if defined(__ANDROID__) && defined(__x86_64__)
#define JNICALL __attribute__((sysv_abi))
#else
#define JNICALL
#endif
#define JNI_TRUE 1
#define JNI_FALSE 0
#define JNI_VERSION_1_6 0x00010006

#endif  // BROWSERX_MOBILE_CHECK_JNI_H_
