/*
    SDL - Simple DirectMedia Layer
    Copyright (C) 2008 Edgar Simo

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

    Sam Lantinga
    slouken@libsdl.org
*/
#include "SDL_config.h"

#ifdef SDL_HAPTIC_IOKIT

#include "SDL_haptic.h"
#include "../SDL_syshaptic.h"
#include "SDL_joystick.h"
#include "../../joystick/SDL_sysjoystick.h" /* For the real SDL_Joystick */
/*#include "../../joystick/dawrin/SDL_sysjoystick_c.h"*/ /* For joystick hwdata */ 

#include <ForceFeedback/ForceFeedback.h>
#include <ForceFeedback/ForceFeedbackConstants.h>


#define MAX_HAPTICS  32


/*
 * List of available haptic devices.
 */
static struct
{
   io_service_t dev;
   SDL_Haptic *haptic;
} SDL_hapticlist[MAX_HAPTICS];


/*
 * Haptic system hardware data.
 */
struct haptic_hwdata
{
   FFDeviceObjectReference device; /* Hardware device. */
};


/*
 * Haptic system effect data.
 */
struct haptic_hweffect
{
   FFEffectObjectReference ref; /* Reference. */
   struct FFEFFECT effect; /* Hardware effect. */
};


/*
 * Initializes the haptic subsystem.
 */
int
SDL_SYS_HapticInit(void)
{
   int numhaptics;
   IOReturn result;
   io_iterator_t iter;
   CFDictionaryRef match;
   io_sercive_t device;

   /* Get HID devices. */
   match = IOServiceMatching(kIOHIDDeviceKey);
   if (match == NULL) {
      SDL_SetError("Haptic: Failed to get IOServiceMatching.");
      return -1;
   }

   /* Now search I/O Registry for matching devices. */
   result = IOServiceGetMatchingServices(kIOMasterPortDefault, match, &iter);
   if (result != kIOReturnSuccess) {
      SDL_SetError("Haptic: Couldn't create a HID object iterator.");
      return -1;
   }
   /* IOServiceGetMatchingServices consumes dictionary. */

   numhaptics = 0;
   while ((device = IOIteratorNext(iter)) != IO_OBJECT_NULL) {

      /* Check for force feedback. */
      if (FFIsForceFeedback(device) == FF_OK) {
         SDL_hapticlist[numhaptics].dev = device;
         SDL_hapticlist[numhaptics].haptic = NULL;
         numhaptics++;
      }

      /* Reached haptic limit. */
      if (numhaptics >= MAX_HAPTICS)
         break;
   }
   IOObjectRelease(iter);

   return numhaptics;
}


/*
 * Return the name of a haptic device, does not need to be opened.
 */
const char *
SDL_SYS_HapticName(int index)
{
   return NULL;
}


#define FF_TEST(ff, s) \
if (features.supportedEffects & ff) supported |= s
/*
 * Gets supported features.
 */
static unsigned int
GetSupportedFeatures(FFDeviceObjectReference device,
                     int *neffects, int *nplaying)
{
   HRESULT ret;
   FFCAPABILITIES features;
   unsigned int supported;
   Uint32 val;

   ret = FFDeviceGetForceFeedbackCapabilities(device, &features);
   if (ret != FF_OK) {
      SDL_SetError("Haptic: Unable to get device's supported features.");
      return 0;
   }

   supported = 0;

   /* Get maximum effects. */
   *neffects = features.storageCapacity;
   *nplaying = features.playbackCapacity;

   /* Test for effects. */
   FF_TEST(FFCAP_ET_CONSTANTFORCE, SDL_HAPTIC_CONSTANT);
   FF_TEST(FFCAP_ET_RAMPFORCE,     SDL_HAPTIC_RAMP);
   FF_TEST(FFCAP_ET_SQUARE,        SDL_HAPTIC_SQUARE);
   FF_TEST(FFCAP_ET_SINE,          SDL_HAPTIC_SINE);
   FF_TEST(FFCAP_ET_TRIANGLE,      SDL_HAPTIC_TRIANGLE);
   FF_TEST(FFCAP_ET_SAWTOOTHUP,    SDL_HAPTIC_SAWTOOTHUP);
   FF_TEST(FFCAP_ET_SAWTOOTHDOWN,  SDL_HAPTIC_SAWTOOTHDOWN);
   FF_TEST(FFCAP_ET_SPRING,        SDL_HAPTIC_SPRING);
   FF_TEST(FFCAP_ET_DAMPER,        SDL_HAPTIC_DAMPER);
   FF_TEST(FFCAP_ET_INERTIA,       SDL_HAPTIC_INERTIA);
   FF_TEST(FFCAP_ET_FRICTION,      SDL_HAPTIC_FRICTION);
   FF_TEST(FFCAP_ET_CUSTOMFORCE,   SDL_HAPTIC_CUSTOM);

   /* Check if supports gain. */
   ret = FFDeviceGetForceFeedbackProperty(device, FFPROP_FFGAIN,
                                          val, sizeof(val));
   if (ret == FF_OK) supported |= SDL_HAPTIC_GAIN;
   else if (ret != FFERR_UNSUPPORTED) {
      SDL_SetError("Haptic: Unable to get if device supports gain.");
      return 0;
   }

   /* Checks if supports autocenter. */
   ret = FFDeviceGetForceFeedbackProperty(device, FFPROP_FFAUTOCENTER,
                                          val, sizeof(val));
   if (ret == FF_OK) supported |= SDL_HAPTIC_AUTOCENTER;
   else if (ret != FFERR_UNSUPPORTED) {
      SDL_SetError("Haptic: Unable to get if device supports autocenter.");
      return 0;
   }

   /* Always supported features. */
   supported |= SDL_HAPTIC_STATUS;
   return supported;
}


/*
 * Opens the haptic device from the file descriptor.
 */
static int
SDL_SYS_HapticOpenFromService(SDL_Haptic * haptic, io_service_t service)
{
   /* Allocate the hwdata */
   haptic->hwdata = (struct haptic_hwdata *)
         SDL_malloc(sizeof(*haptic->hwdata));
   if (haptic->hwdata == NULL) {
      SDL_OutOfMemory();
      goto creat_err;
   }
   SDL_memset(haptic->hwdata, 0, sizeof(*haptic->hwdata));
  
   /* Open the device */
   if (FFCreateDevice( &service, &haptic->hwdata->device ) != FF_OK) {
      SDL_SetError("Haptic: Unable to create device from service.");
      goto creat_err;
   }

   /* Get supported features. */
   haptic->supported = GetSupportedFeatures(haptic->hwdata->device,
                                            &haptic->neffects, &haptic->nplaying);
   if (haptic->supported == 0) { /* Error since device supports nothing. */
      goto open_err;
   }
   haptic->effects = (struct haptic_effect *)
         SDL_malloc(sizeof(struct haptic_effect) * haptic->neffects);
   if (haptic->effects == NULL) {
      SDL_OutOfMemory();
      goto open_err;
   }
   /* Clear the memory */
   SDL_memset(haptic->effects, 0,
         sizeof(struct haptic_effect) * haptic->neffects);
   
   return 0;
   
   /* Error handling */
open_err:
   FFReleaseDevice(haptic->hwdata->device);
creat_err:
   if (haptic->hwdata != NULL) {
      free(haptic->hwdata);
      haptic->hwdata = NULL;                                              
   }
   return -1;

}


/*
 * Opens a haptic device for usage.
 */
int
SDL_SYS_HapticOpen(SDL_Haptic * haptic)
{
   return SDL_SYS_HapticOpenFromService(haptic,
                SDL_hapticlist[haptic->index].device);
}


/*
 * Opens a haptic device from first mouse it finds for usage.
 */
int
SDL_SYS_HapticMouse(void)
{
   return -1;
}


/*
 * Checks to see if a joystick has haptic features.
 */
int
SDL_SYS_JoystickIsHaptic(SDL_Joystick * joystick)
{
   return SDL_FALSE;
}


/*
 * Checks to see if the haptic device and joystick and in reality the same.
 */
int
SDL_SYS_JoystickSameHaptic(SDL_Haptic * haptic, SDL_Joystick * joystick)
{
   if (SDL_strcmp(joystick->name,haptic->name)==0) {
      return 1;
   }
   return 0;
}


/*
 * Opens a SDL_Haptic from a SDL_Joystick.
 */
int
SDL_SYS_HapticOpenFromJoystick(SDL_Haptic * haptic, SDL_Joystick * joystick)
{
   return -1;
}


/*
 * Closes the haptic device.
 */
void
SDL_SYS_HapticClose(SDL_Haptic * haptic)
{
   int i;

   if (haptic->hwdata) {

      /* Clean up */
      FFReleaseDevice(haptic->hwdata->device);

      /* Free */
      SDL_free(haptic->hwdata);
      haptic->hwdata = NULL;
      for (i=0; i<haptic->neffects; i++) {
         if (haptic->effects[i].hweffect != NULL)
            SDL_free(haptic->effects[i].hweffect->effect.lpvTypeSpecificParams);
      }
      SDL_free(haptic->effects);
      haptic->neffects = 0;
   }
}


/* 
 * Clean up after system specific haptic stuff
 */
void
SDL_SYS_HapticQuit(void)
{
   int i;

   for (i=0; i < SDL_numhaptics; i++) {
      IOObjectRelease(SDL_hapticlist[i].dev);
      /* TODO free effects. */
   }
}


/*
 * Sets the direction.
 */
static int
SDL_SYS_SetDirection( FFEFFECT * effect, SDL_HapticDirection *dir, int axes )
{
   LONG *dir;
   dir = SDL_malloc( sizeof(LONG) * axes );
   if (dir == NULL) {
      SDL_OutOfMemory();
      return -1;
   }
   SDL_memset( dir, 0, sizeof(LONG) * axes );
   effect->rglDirection = dir;

   switch (dir->type) {
      case SDL_HAPTIC_POLAR:
         effect->dwFlags |= FFEFF_POLAR;
         dir[0] = dir->dir[0];
         return 0;
      case SDL_HAPTIC_CARTESIAN:
         effects->dwFlags |= FFEFF_CARTESIAN;
         dir[0] = dir->dir[0];
         dir[1] = dir->dir[1];
         dir[2] = dir->dir[2];
         return 0;
      case SDL_HAPTIC_SHPERICAL:
         effects->dwFlags |= FFEFF_SPHERICAL;
         dir[0] = dir->dir[0];
         dir[1] = dir->dir[1];
         dir[2] = dir->dir[2];
         return 0;

      default:
         SDL_SetError("Haptic: Unknown direction type.");
         return -1;
   }
}

#define CONVERT(x)   (((x)*10000) / 0xFFFF )
/*
 * Creates the FFStruct
 */
static int
SDL_SYS_ToFFEFFECT( FFEFFECT * dest, SDL_HapticEffect * src )
{
   FFCONSTANTFORCE *constant;
   FFPERIODIC *periodic;
   FFCONDITION *condition;
   FFRAMPFORCE *ramp;
   FFCUSTOMFORCE *custom;
   SDL_HapticConstant *hap_constant;
   SDL_HapticPeriodic *hap-periodic;
   SDL_HapticCondition *hap_condition;
   SDL_HapticRamp *hap_ramp;

   /* Set global stuff. */
   SDL_memset(dest, 0, sizeof(FFEFFECT));
   dest->dwSize = sizeof(FFEFFECT); /* Set the structure size. */
   dest->dwSamplePeriod = 0; /* Not used by us. */
   dest->dwGain = 10000; /* Gain is set globally, not locally. */
   dest->lpEnvelope.dwSize = sizeof(FFENVELOPE); /* Always should be this. */

   switch (src->type) {
      case SDL_HAPTIC_CONSTANT:
         hap_constant = &src->constant;
         constant = SDL_malloc( sizeof(FFCONSTANTFORCE) );

         /* Specifics */
         constant->lMagnitude = CONVERT(hap_constant->level);
         dest->cbTypeSpecificParams = sizeof(FFCONSTANTFORCE); 
         dest->lpvTypeSpecificParams = constant;

         /* Generics */
         dest->dwDuration = src->length * 1000; /* In microseconds. */
         dest->dwTriggerButton = FFJOFS_BUTTON(hap_constant->button);
         dest->dwTriggerRepeatInterval = hap_constant->interval;
         dest->dwStartDelay = src->delay * 1000; /* In microseconds. */

         /* Axes */
         dest->cAxes = 2; /* TODO handle */
         dest->rgdwAxes = 0;

         /* Direction. */
         if (SDL_SYS_SetDirection(dest, hap_constant->direction, dest->cAxes) < 0) {
            return -1;
         }
         
         /* Envelope */
         dest->lpEnvelope.dwAttackLevel = CONVERT(hap_constant->attack_level);
         dest->lpEnvelope.dwAttackTime = hap_constant->attack_length * 1000;
         dest->lpEnvelope.dwFadeLevel = CONVERT(hap_constant->fade_level);
         dest->lpEnvelope.dwFadeTime = hap_constant->fade_length * 1000;

         break;

         /* TODO finish */

      case SDL_HAPTIC_SINE:
      case SDL_HAPTIC_SQUARE:
      case SDL_HAPTIC_TRIANGLE:
      case SDL_HAPTIC_SAWTOOTHUP:
      case SDL_HAPTIC_SAWTOOTHDOWN:
         periodic = &src->periodic;

         break;

      case SDL_HAPTIC_SPRING:
      case SDL_HAPTIC_DAMPER:
      case SDL_HAPTIC_INERTIA:
      case SDL_HAPTIC_FRICTION:
         condition = &src->condition;

         break;

      case SDL_HAPTIC_RAMP:
         ramp = &src->ramp;

         break;


      default:
         SDL_SetError("Haptic: Unknown effect type.");
         return -1;
   }

   return 0;
}


/*
 * Gets the effect type from the generic SDL haptic effect wrapper.
 */
CFUUIDRef SDL_SYS_HapticEffectType(struct haptic_effect * effect)
{
   switch (effect->effect->type) {
      case SDL_HAPTIC_CONSTANT:
         return kFFEffectType_ConstantForce_ID;

      case SDL_HAPTIC_RAMP:
         return kFFEffectType_RampForce_ID;

      case SDL_HAPTIC_SQUARE:
         return kFFEffectType_Square_ID;

      case SDL_HAPTIC_SINE:
         return kFFEffectType_Sine_ID;

      case SDL_HAPTIC_TRIANGLE;
         return kFFEffectType_Triangle_ID;

      case SDL_HAPTIC_SAWTOOTHUP:
         return kFFEffectType_SawtoothUp_ID;

      case SDL_HAPTIC_SAWTOOTHDOWN:
         return kFFEffectType_SawtoothDown_ID;

      case SDL_HAPTIC_SPRING:
         return kFFEffectType_Spring_ID;

      case SDL_HAPTIC_DAMPER:
         return kFFEffectType_Damper_ID;

      case SDL_HAPTIC_INERTIA:
         return kFFEffectType_Inertia_ID;

      case SDL_HAPTIC_FRICTION:
         return kFFEffectType_Friction_ID;

      case SDL_HAPTIC_CUSTOM:
         return kFFEffectType_CustomForce_ID;

      default:
         SDL_SetError("Haptic: Unknown effect type.");
         return NULL;
   }
}


/*
 * Creates a new haptic effect.
 */
int
SDL_SYS_HapticNewEffect(SDL_Haptic * haptic, struct haptic_effect * effect,
      SDL_HapticEffect * base)
{
   HRESULT ret;
   CFUUIDRef type;

   /* Alloc the effect. */
   effect->hweffect = (struct haptic_hweffect *)
         SDL_malloc(sizeof(struct haptic_hweffect));
   if (effect->hweffect == NULL) {
      SDL_OutOfMemory();
      return -1;
   }

   /* Get the type. */
   type = SDL_SYS_HapticEffectType(effect);
   if (type == NULL) {
      SDL_free(effect->hweffect);
      effect->hweffect = NULL;
      return -1;
   }

   /* Get the effect. */
   if (SDL_SYS_ToFFEFFECT( &effect->hweffect->effect, &haptic_effect->effect ) < 0) {
      /* TODO cleanup alloced stuff. */
      return -1;
   }

   ret = FFDeviceCreateEffect( haptic->hwdata->device, type,
         &effect->hweffect->effect, &effect->hweffect->ref );
}


/*
 * Updates an effect.
 */
int SDL_SYS_HapticUpdateEffect(SDL_Haptic * haptic,
      struct haptic_effect * effect, SDL_HapticEffect * data)
{
   /* TODO */
   return -1;
}


/*
 * Runs an effect.
 */
int
SDL_SYS_HapticRunEffect(SDL_Haptic * haptic, struct haptic_effect * effect,
                        Uint32 iterations)
{
   HRESULT ret;
   Uint32 iter;

   /* Check if it's infinite. */
   if (iterations == SDL_HAPTIC_INFINITY) {
      iter = INFINITE;
   }
   else
      iter = iterations;

   /* Run the effect. */
   ret = FFEffectStart(effect->hweffect->ref, iter, 0);
   if (ret != FF_OK) {
      SDL_SetError("Haptic: Unable to run the effect.");
      return -1;
   }

   return 0;
}


/*
 * Stops an effect.
 */
int
SDL_SYS_HapticStopEffect(SDL_Haptic * haptic, struct haptic_effect * effect)
{
   HRESULT ret;

   ret = FFEffectStop(effect->hweffect->ref);
   if (ret != FF_OK) {
      SDL_SetError("Haptic: Unable to stop the effect.");
      return -1;
   }

   return 0;
}


/*
 * Frees the effect.
 */
void
SDL_SYS_HapticDestroyEffect(SDL_Haptic * haptic, struct haptic_effect * effect)
{
   HRESULT ret;

   ret = FFDeviceReleaseEffect(haptic->hwdata->device, effect->hweffect->ref);
   if (ret != FF_OK) {
      SDL_SetError("Haptic: Error removing the effect from the device.");
   }
   SDL_free(effect->hweffect->effect.lpvTypeSpecificParams);
   effect->hweffect->effect.lpvTypeSpecificParams = NULL;
   SDL_free(effect->hweffect);
   effect->hweffect = NULL;
}


/*
 * Gets the status of a haptic effect.
 */
int
SDL_SYS_HapticGetEffectStatus(SDL_Haptic * haptic, struct haptic_effect * effect)
{
   HRESULT ret;
   FFEffectStatusFlag status;

   ret = FFEffectGetEffectStatus(effect->hweffect.ref, &status);
   if (ret != FF_OK) {
      SDL_SetError("Haptic: Unable to get effect status.");
      return -1;
   }

   if (status == 0) return SDL_FALSE;
   return SDL_TRUE; /* Assume it's playing or emulated. */
}


/*
 * Sets the gain.
 */
int
SDL_SYS_HapticSetGain(SDL_Haptic * haptic, int gain)
{
   HRESULT ret;
   Uint32 val;

   val = gain * 100; /* Mac OS X uses 0 to 10,000 */
   ret = FFDeviceSetForceFeedbackProperty(haptic->hwdata->device, FFPROP_FFGAIN, &val);
   if (ret != FF_OK) {
      SDL_SetError("Haptic: Error setting gain.");
      return -1;
   }

   return 0;
}


/*
 * Sets the autocentering.
 */
int
SDL_SYS_HapticSetAutocenter(SDL_Haptic * haptic, int autocenter)
{
   HRESULT ret;
   Uint32 val;

   /* Mac OS X only has 0 (off) and 1 (on) */
   if (autocenter == 0) val = 0;
   else val = 1;

   ret = FFDeviceSetForceFeedbackProperty(haptic->hwdata->device,
               FFPROP_FFAUTOCENTER, &val);
   if (ret != FF_OK) {
      SDL_SetError("Haptic: Error setting autocenter.");
      return -1;
   }
  
   return 0;

}


#endif /* SDL_HAPTIC_LINUX */
