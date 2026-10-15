// Added: entire file. The touch overlay's Android side: haptics, through
// org.openjkdf2.app.TouchHaptics (the screen's own haptic feedback, falling
// back to a short vibration). Optional: if the class or the call isn't
// there, nothing happens.

#include "Platform/Touch/touchUiSdl.h"

#if defined(TOUCHUI_ENABLED) && defined(TARGET_ANDROID)

#include <jni.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_system.h>

static jclass touchUiAndroid_cls = NULL;
static jmethodID touchUiAndroid_mid = NULL;
static int touchUiAndroid_bLooked = 0;

void touchUiPlat_Haptic(int kinds)
{
    JNIEnv* env = (JNIEnv*)SDL_GetAndroidJNIEnv();
    if (!env) return;
    if (!touchUiAndroid_bLooked) {
        // (once, on the game's thread, which can see the app's classes)
        touchUiAndroid_bLooked = 1;
        jclass cls = (*env)->FindClass(env, "org/openjkdf2/app/TouchHaptics");
        if (cls && !(*env)->ExceptionCheck(env)) {
            touchUiAndroid_cls = (jclass)(*env)->NewGlobalRef(env, cls);
            touchUiAndroid_mid = (*env)->GetStaticMethodID(env, touchUiAndroid_cls, "perform", "(Landroid/app/Activity;I)V");
        }
        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
            touchUiAndroid_mid = NULL;
        }
        if (cls) (*env)->DeleteLocalRef(env, cls);
    }
    if (!touchUiAndroid_mid) return;
    jobject activity = (jobject)SDL_GetAndroidActivity();
    if (!activity) return;
    (*env)->CallStaticVoidMethod(env, touchUiAndroid_cls, touchUiAndroid_mid, activity, (jint)kinds);
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, activity);
}

#endif // TOUCHUI_ENABLED && TARGET_ANDROID
