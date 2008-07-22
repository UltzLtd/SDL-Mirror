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

#include <IOKit/IOKitLib.h>
#include <IOKit/hid/IOHIDKeys.h>
#include <ForceFeedback/ForceFeedback.h>
#include <ForceFeedback/ForceFeedbackConstants.h>


#define MAX_HAPTICS  32


/*
 * List of available haptic devices.
 */
static struct
{
   char name[256];
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
 * Prototypes.
 */
static void SDL_SYS_HapticFreeFFEFFECT(FFEFFECT * effect, int type);
static int HIDGetDeviceProduct(io_service_t dev, char * name);


/* 
 * Like strerror but for force feedback errors.
 */
static const char *
FFStrError(HRESULT err)
{
   switch (err) {
      case FFERR_DEVICEFULL:
         return "device full";
      /*case FFERR_DEVICENOTREG:
         return "device not registered";*/
      case FFERR_DEVICEPAUSED:
         return "device paused";
      case FFERR_DEVICERELEASED:
         return "device released";
      case FFERR_EFFECTPLAYING:
         return "effect playing";
      case FFERR_EFFECTTYPEMISMATCH:
         return "effect type mismatch";
      case FFERR_EFFECTTYPENOTSUPPORTED:
         return "effect type not supported";
      case FFERR_GENERIC:
         return "undetermined error";
      case FFERR_HASEFFECTS:
         return "device has effects";
      case FFERR_INCOMPLETEEFFECT:
         return "incomplete effect";
      case FFERR_INTERNAL:
         return "internal fault";
      case FFERR_INVALIDDOWNLOADID:
         return "invalid download id";
      case FFERR_INVALIDPARAM:
         return "invalid parameter";
      case FFERR_MOREDATA:
         return "more data";
      case FFERR_NOINTERFACE:
         return "interface not supported";
      case FFERR_NOTDOWNLOADED:
         return "effect is not downloaded";
      case FFERR_NOTINITIALIZED:
         return "object has not been initialized";
      case FFERR_OUTOFMEMORY:
         return "out of memory";
      case FFERR_UNPLUGGED:
         return "device is unplugged";
      case FFERR_UNSUPPORTED:
         return "function call unsupported";
      case FFERR_UNSUPPORTEDAXIS:
         return "axis unsupported";

      default:
         return "unknown error";
   }
}


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
   io_service_t device;

   /* Clear all the memory. */
   SDL_memset(SDL_hapticlist, 0, sizeof(SDL_hapticlist));

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
         HIDGetDeviceProduct(device, SDL_hapticlist[numhaptics].name);
         SDL_hapticlist[numhaptics].dev = device;
         SDL_hapticlist[numhaptics].haptic = NULL;
         numhaptics++;
      }
      else { /* Free the unused device. */
         IOObjectRelease(device);
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
   return SDL_hapticlist[index].name;
}

/*
 * Gets the device's product name.
 */
static int
HIDGetDeviceProduct(io_service_t dev, char *name)
{
   CFMutableDictionaryRef hidProperties, usbProperties;
   io_registry_entry_t parent1, parent2;
   kern_return_t ret;

   hidProperties = usbProperties = 0;

   ret = IORegistryEntryCreateCFProperties(hidDevice, &hidProperties,
                                           kCFAllocatorDefault,
                                           kNilOptions);
   if ((ret != KERN_SUCCESS) || !hidProperties) {
      SDL_SetError("Haptic: Unable to create CFProperties.");
      return -1;
   }

   /* Mac OS X currently is not mirroring all USB properties to HID page so need to look at USB device page also
    * get dictionary for usb properties: step up two levels and get CF dictionary for USB properties
    */
   if ((KERN_SUCCESS ==
            IORegistryEntryGetParentEntry(dev, kIOServicePlane, &parent1))
         && (KERN_SUCCESS ==
            IORegistryEntryGetParentEntry(parent1, kIOServicePlane, &parent2))
         && (KERN_SUCCESS ==
            IORegistryEntryCreateCFProperties(parent2, &usbProperties,
                                              kCFAllocatorDefault,
                                              kNilOptions))) {
      if (usbProperties) {
         CFTypeRef refCF = 0;
         /* get device info
          * try hid dictionary first, if fail then go to usb dictionary
          */


         /* Get product name */
         refCF = CFDictionaryGetValue(hidProperties, CFSTR(kIOHIDProductKey));
         if (!refCF)
            refCF =
               CFDictionaryGetValue(usbProperties, CFSTR("USB Product Name"));
         if (refCF) {
            if (!CFStringGetCString(refCF, name, 256,
                                    CFStringGetSystemEncoding())) {
               SDL_SetError("CFStringGetCString error retrieving pDevice->product.");
               return -1;
            }
         }

         CFRelease(usbProperties);
      }
      else {
         SDL_SetError("IORegistryEntryCreateCFProperties failed to create usbProperties.");
         return -1;
      }

      /* Release stuff. */
      if (kIOReturnSuccess != IOObjectRelease(parent2)) {
         SDL_SetError("IOObjectRelease error with parent2.");
      }
      if (kIOReturnSuccess != IOObjectRelease(parent1))  {
         SDL_SetError("IOObjectRelease error with parent1.");
      }
   }
   else {
      SDL_SetError("Haptic: Error getting registry entries.");
      return -1;
   }

   return 0;
}


#define FF_TEST(ff, s) \
if (features.supportedEffects & ff) supported |= s
/*
 * Gets supported features.
 */
static unsigned int
GetSupportedFeatures(FFDeviceObjectReference device,
                     int *neffects, int *nplaying, int *naxes)
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
                                          &val, sizeof(val));
   if (ret == FF_OK) supported |= SDL_HAPTIC_GAIN;
   else if (ret != FFERR_UNSUPPORTED) {
      SDL_SetError("Haptic: Unable to get if device supports gain: %s.",
                   FFStrError(ret));
      return 0;
   }

   /* Checks if supports autocenter. */
   ret = FFDeviceGetForceFeedbackProperty(device, FFPROP_AUTOCENTER,
                                          &val, sizeof(val));
   if (ret == FF_OK) supported |= SDL_HAPTIC_AUTOCENTER;
   else if (ret != FFERR_UNSUPPORTED) {
      SDL_SetError("Haptic: Unable to get if device supports autocenter: %s.",
                   FFStrError(ret));
      return 0;
   }

   /* Check for axes, we have an artificial limit on axes */
   *naxes = ((features.numFfAxes) > 3) ?
         3 : features.numFfAxes;

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
   HRESULT ret;

   /* Allocate the hwdata */
   haptic->hwdata = (struct haptic_hwdata *)
         SDL_malloc(sizeof(*haptic->hwdata));
   if (haptic->hwdata == NULL) {
      SDL_OutOfMemory();
      goto creat_err;
   }
   SDL_memset(haptic->hwdata, 0, sizeof(*haptic->hwdata));
  
   /* Open the device */
   ret = FFCreateDevice( service, &haptic->hwdata->device);
   if (ret != FF_OK) {
      SDL_SetError("Haptic: Unable to create device from service: %s.",
                   FFStrError(ret));
      goto creat_err;
   }

   /* Get supported features. */
   haptic->supported = GetSupportedFeatures(haptic->hwdata->device,
                                            &haptic->neffects, &haptic->nplaying,
                                            &haptic->naxes);
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
                SDL_hapticlist[haptic->index].dev);
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

      /* Free the effects. */
      for (i=0; i<haptic->neffects; i++) {        
         if (haptic->effects[i].hweffect != NULL) {
            SDL_SYS_HapticFreeFFEFFECT(&haptic->effects[i].hweffect->effect,
                                       haptic->effects[i].effect.type);
            SDL_free(haptic->effects[i].hweffect);
         } 
      }    
      SDL_free(haptic->effects);
      haptic->neffects = 0;

      /* Clean up */
      FFReleaseDevice(haptic->hwdata->device);

      /* Free */
      SDL_free(haptic->hwdata);
      haptic->hwdata = NULL;
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
      /* Opened and not closed haptics are leaked, this is on purpose.
       * Close your haptic devices after usage. */

      /* Free the io_service_t */
      IOObjectRelease(SDL_hapticlist[i].dev);
   }
}


/*
 * Sets the direction.
 */
static int
SDL_SYS_SetDirection( FFEFFECT * effect, SDL_HapticDirection *dir, int naxes )
{
   LONG *rglDir;

   /* Handle no axes a part. */
   if (naxes == 0) {
      effect->rglDirection = NULL;
      return 0;
   }

   /* Has axes. */
   rglDir = SDL_malloc( sizeof(LONG) * naxes );
   if (rglDir == NULL) {
      SDL_OutOfMemory();
      return -1;
   }
   SDL_memset( rglDir, 0, sizeof(LONG) * naxes );
   effect->rglDirection = rglDir;

   switch (dir->type) {
      case SDL_HAPTIC_POLAR:
         effect->dwFlags |= FFEFF_POLAR;
         rglDir[0] = dir->dir[0];
         return 0;
      case SDL_HAPTIC_CARTESIAN:
         effect->dwFlags |= FFEFF_CARTESIAN;
         rglDir[0] = dir->dir[0];
         rglDir[1] = dir->dir[1];
         rglDir[2] = dir->dir[2];
         return 0;
      case SDL_HAPTIC_SPHERICAL:
         effect->dwFlags |= FFEFF_SPHERICAL;
         rglDir[0] = dir->dir[0];
         rglDir[1] = dir->dir[1];
         rglDir[2] = dir->dir[2];
         return 0;

      default:
         SDL_SetError("Haptic: Unknown direction type.");
         return -1;
   }
}

#define CONVERT(x)   (((x)*10000) / 0xFFFF )
/*
 * Creates the FFEFFECT from a SDL_HapticEffect.
 */
static int
SDL_SYS_ToFFEFFECT( SDL_Haptic * haptic, FFEFFECT * dest, SDL_HapticEffect * src )
{
   int i;
   FFCONSTANTFORCE *constant;
   FFPERIODIC *periodic;
   FFCONDITION *condition; /* Actually an array of conditions - one per axis. */
   FFRAMPFORCE *ramp;
   FFCUSTOMFORCE *custom;
   FFENVELOPE *envelope;
   SDL_HapticConstant *hap_constant;
   SDL_HapticPeriodic *hap_periodic;
   SDL_HapticCondition *hap_condition;
   SDL_HapticRamp *hap_ramp;
   SDL_HapticCustom *hap_custom;
   DWORD *axes;

   /* Set global stuff. */
   SDL_memset(dest, 0, sizeof(FFEFFECT));
   dest->dwSize = sizeof(FFEFFECT); /* Set the structure size. */
   dest->dwSamplePeriod = 0; /* Not used by us. */
   dest->dwGain = 10000; /* Gain is set globally, not locally. */

   /* Envelope. */
   envelope = SDL_malloc( sizeof(FFENVELOPE) );
   if (envelope == NULL) {
      SDL_OutOfMemory();
      return -1;
   }
   SDL_memset(envelope, 0, sizeof(FFENVELOPE));
   dest->lpEnvelope = envelope;
   envelope->dwSize = sizeof(FFENVELOPE); /* Always should be this. */

   /* Axes. */
   dest->cAxes = haptic->naxes;
   if (dest->cAxes > 0) {
      axes = SDL_malloc(sizeof(DWORD) * dest->cAxes);
      if (axes == NULL) {
         SDL_OutOfMemory();
         return -1;
      }
      axes[0] = FFJOFS_X; /* Always at least one axis. */
      if (dest->cAxes > 1) {
         axes[1] = FFJOFS_Y;
      }
      if (dest->cAxes > 2) {
         axes[2] = FFJOFS_Z;
      }
      dest->rgdwAxes = axes;
   }


   /* The big type handling switch, even bigger then linux's version. */
   switch (src->type) {
      case SDL_HAPTIC_CONSTANT:
         hap_constant = &src->constant;
         constant = SDL_malloc( sizeof(FFCONSTANTFORCE) );
         if (constant == NULL) {
            SDL_OutOfMemory();
            return -1;
         }
         SDL_memset(constant, 0, sizeof(FFCONSTANTFORCE));

         /* Specifics */
         constant->lMagnitude = CONVERT(hap_constant->level);
         dest->cbTypeSpecificParams = sizeof(FFCONSTANTFORCE); 
         dest->lpvTypeSpecificParams = constant;

         /* Generics */
         dest->dwDuration = hap_constant->length * 1000; /* In microseconds. */
         dest->dwTriggerButton = FFJOFS_BUTTON(hap_constant->button);
         dest->dwTriggerRepeatInterval = hap_constant->interval;
         dest->dwStartDelay = hap_constant->delay * 1000; /* In microseconds. */

         /* Direction. */
         if (SDL_SYS_SetDirection(dest, &hap_constant->direction, dest->cAxes) < 0) {
            return -1;
         }
         
         /* Envelope */
         envelope->dwAttackLevel = CONVERT(hap_constant->attack_level);
         envelope->dwAttackTime = hap_constant->attack_length * 1000;
         envelope->dwFadeLevel = CONVERT(hap_constant->fade_level);
         envelope->dwFadeTime = hap_constant->fade_length * 1000;

         break;

      case SDL_HAPTIC_SINE:
      case SDL_HAPTIC_SQUARE:
      case SDL_HAPTIC_TRIANGLE:
      case SDL_HAPTIC_SAWTOOTHUP:
      case SDL_HAPTIC_SAWTOOTHDOWN:
         hap_periodic = &src->periodic;
         periodic = SDL_malloc(sizeof(FFPERIODIC));
         if (periodic == NULL) {
            SDL_OutOfMemory();
            return -1;
         }
         SDL_memset(periodic, 0, sizeof(FFPERIODIC));

         /* Specifics */
         periodic->dwMagnitude = CONVERT(hap_periodic->magnitude);
         periodic->lOffset = CONVERT(hap_periodic->offset);
         periodic->dwPhase = hap_periodic->phase;
         periodic->dwPeriod = hap_periodic->period * 1000;
         dest->cbTypeSpecificParams = sizeof(FFPERIODIC);
         dest->lpvTypeSpecificParams = periodic;

         /* Generics */
         dest->dwDuration = hap_periodic->length * 1000; /* In microseconds. */
         dest->dwTriggerButton = FFJOFS_BUTTON(hap_periodic->button);
         dest->dwTriggerRepeatInterval = hap_periodic->interval;
         dest->dwStartDelay = hap_periodic->delay * 1000; /* In microseconds. */
         
         /* Direction. */
         if (SDL_SYS_SetDirection(dest, &hap_periodic->direction, dest->cAxes) < 0) {
            return -1;
         }
         
         /* Envelope */
         envelope->dwAttackLevel = CONVERT(hap_periodic->attack_level);
         envelope->dwAttackTime = hap_periodic->attack_length * 1000;
         envelope->dwFadeLevel = CONVERT(hap_periodic->fade_level);
         envelope->dwFadeTime = hap_periodic->fade_length * 1000;

         break;

      case SDL_HAPTIC_SPRING:
      case SDL_HAPTIC_DAMPER:
      case SDL_HAPTIC_INERTIA:
      case SDL_HAPTIC_FRICTION:
         hap_condition = &src->condition;
         condition = SDL_malloc(sizeof(FFCONDITION) * dest->cAxes);
         if (condition == NULL) {
            SDL_OutOfMemory();
            return -1;
         }
         SDL_memset(condition, 0, sizeof(FFCONDITION));

         /* Specifics */
         for (i=0; i<dest->cAxes; i++) {
            condition[i].lOffset = CONVERT(hap_condition->center[i]);
            condition[i].lPositiveCoefficient = CONVERT(hap_condition->right_coeff[i]);
            condition[i].lNegativeCoefficient = CONVERT(hap_condition->left_coeff[i]);
            condition[i].dwPositiveSaturation = CONVERT(hap_condition->right_sat[i]);
            condition[i].dwNegativeSaturation = CONVERT(hap_condition->left_sat[i]);
            condition[i].lDeadBand = CONVERT(hap_condition->deadband[i]);
         }
         dest->cbTypeSpecificParams = sizeof(FFCONDITION) * dest->cAxes;
         dest->lpvTypeSpecificParams = condition;

         /* Generics */
         dest->dwDuration = hap_condition->length * 1000; /* In microseconds. */
         dest->dwTriggerButton = FFJOFS_BUTTON(hap_condition->button);
         dest->dwTriggerRepeatInterval = hap_condition->interval;
         dest->dwStartDelay = hap_condition->delay * 1000; /* In microseconds. */

         /* Direction. */
         if (SDL_SYS_SetDirection(dest, &hap_condition->direction, dest->cAxes) < 0) {
            return -1;                
         }                            
                                      
         /* Envelope */
/* TODO Check is envelope actually used.
         envelope->dwAttackLevel = CONVERT(hap_condition->attack_level);
         envelope->dwAttackTime = hap_condition->attack_length * 1000;
         envelope->dwFadeLevel = CONVERT(hap_condition->fade_level);
         envelope->dwFadeTime = hap_condition->fade_length * 1000;
*/

         break;

      case SDL_HAPTIC_RAMP:
         hap_ramp = &src->ramp;
         ramp = SDL_malloc(sizeof(FFRAMPFORCE));
         if (ramp == NULL) {
            SDL_OutOfMemory();
            return -1;
         }
         SDL_memset(ramp, 0, sizeof(FFRAMPFORCE));

         /* Specifics */
         ramp->lStart = CONVERT(hap_ramp->start);
         ramp->lEnd = CONVERT(hap_ramp->end);
         dest->cbTypeSpecificParams = sizeof(FFRAMPFORCE);
         dest->lpvTypeSpecificParams = ramp;

         /* Generics */
         dest->dwDuration = hap_ramp->length * 1000; /* In microseconds. */
         dest->dwTriggerButton = FFJOFS_BUTTON(hap_ramp->button);
         dest->dwTriggerRepeatInterval = hap_ramp->interval;
         dest->dwStartDelay = hap_ramp->delay * 1000; /* In microseconds. */

         /* Direction. */
         if (SDL_SYS_SetDirection(dest, &hap_ramp->direction, dest->cAxes) < 0) {
            return -1;
         }

         /* Envelope */
         envelope->dwAttackLevel = CONVERT(hap_ramp->attack_level);
         envelope->dwAttackTime = hap_ramp->attack_length * 1000;
         envelope->dwFadeLevel = CONVERT(hap_ramp->fade_level);
         envelope->dwFadeTime = hap_ramp->fade_length * 1000;

         break;

      case SDL_HAPTIC_CUSTOM:
         hap_custom = &src->custom;
         custom = SDL_malloc(sizeof(FFCUSTOMFORCE));
         if (custom == NULL) {
            SDL_OutOfMemory();
            return -1;
         }
         SDL_memset(custom, 0, sizeof(FFCUSTOMFORCE));

         /* Specifics */
         custom->cChannels = hap_custom->channels;
         custom->dwSamplePeriod = hap_custom->period * 1000;
         custom->cSamples = hap_custom->samples;
         custom->rglForceData = SDL_malloc(sizeof(LONG)*custom->cSamples*custom->cChannels);
         for (i=0; i<hap_custom->samples*hap_custom->channels; i++) { /* Copy data. */
            custom->rglForceData[i] = CONVERT(hap_custom->data[i]);
         }
         dest->cbTypeSpecificParams = sizeof(FFCUSTOMFORCE);
         dest->lpvTypeSpecificParams = custom;

         /* Generics */
         dest->dwDuration = hap_custom->length * 1000; /* In microseconds. */
         dest->dwTriggerButton = FFJOFS_BUTTON(hap_custom->button);
         dest->dwTriggerRepeatInterval = hap_custom->interval;
         dest->dwStartDelay = hap_custom->delay * 1000; /* In microseconds. */

         /* Direction. */
         if (SDL_SYS_SetDirection(dest, &hap_custom->direction, dest->cAxes) < 0) {
            return -1;
         }
         
         /* Envelope */
         envelope->dwAttackLevel = CONVERT(hap_custom->attack_level);
         envelope->dwAttackTime = hap_custom->attack_length * 1000;
         envelope->dwFadeLevel = CONVERT(hap_custom->fade_level);
         envelope->dwFadeTime = hap_custom->fade_length * 1000;

         break;


      default:
         SDL_SetError("Haptic: Unknown effect type.");
         return -1;
   }

   return 0;
}


/*
 * Frees an FFEFFECT allocated by SDL_SYS_ToFFEFFECT.
 */
static void
SDL_SYS_HapticFreeFFEFFECT( FFEFFECT * effect, int type )
{
   FFCUSTOMFORCE *custom;

   if (effect->lpEnvelope != NULL) {
      SDL_free(effect->lpEnvelope);
      effect->lpEnvelope = NULL;
   }
   if (effect->rgdwAxes != NULL) {
      SDL_free(effect->rgdwAxes);
      effect->rgdwAxes = NULL;
   }
   if (effect->lpvTypeSpecificParams != NULL) {
      if (type == SDL_HAPTIC_CUSTOM) { /* Must free the custom data. */
         custom = (FFCUSTOMFORCE*) effect->lpvTypeSpecificParams;
         SDL_free(custom->rglForceData);
         custom->rglForceData = NULL;
      }
      SDL_free(effect->lpvTypeSpecificParams);
      effect->lpvTypeSpecificParams = NULL;
   }
   if (effect->rglDirection != NULL) {
      SDL_free(effect->rglDirection);
      effect->rglDirection = NULL;
   }
}


/*
 * Gets the effect type from the generic SDL haptic effect wrapper.
 */
CFUUIDRef
SDL_SYS_HapticEffectType(struct haptic_effect * effect)
{
   switch (effect->effect.type) {
      case SDL_HAPTIC_CONSTANT:
         return kFFEffectType_ConstantForce_ID;

      case SDL_HAPTIC_RAMP:
         return kFFEffectType_RampForce_ID;

      case SDL_HAPTIC_SQUARE:
         return kFFEffectType_Square_ID;

      case SDL_HAPTIC_SINE:
         return kFFEffectType_Sine_ID;

      case SDL_HAPTIC_TRIANGLE:
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
      goto err_hweffect;
   }

   /* Get the type. */
   type = SDL_SYS_HapticEffectType(effect);
   if (type == NULL) {
      goto err_hweffect;
   }

   /* Get the effect. */
   if (SDL_SYS_ToFFEFFECT(haptic, &effect->hweffect->effect, base) < 0) {
      goto err_effectdone;
   }

   /* Create the actual effect. */
   ret = FFDeviceCreateEffect(haptic->hwdata->device, type,
         &effect->hweffect->effect, &effect->hweffect->ref);
   if (ret != FF_OK) {
      SDL_SetError("Haptic: Unable to create effect: %s.", FFStrError(ret));
      goto err_effectdone;
   }

   return 0;

err_effectdone:
   SDL_SYS_HapticFreeFFEFFECT(&effect->hweffect->effect, base->type);
err_hweffect:
   if (effect->hweffect != NULL) {
      SDL_free(effect->hweffect);
      effect->hweffect = NULL;
   }
   return -1;
}


/*
 * Updates an effect.
 */
int
SDL_SYS_HapticUpdateEffect(SDL_Haptic * haptic,
      struct haptic_effect * effect, SDL_HapticEffect * data)
{
   HRESULT ret;
   FFEffectParameterFlag flags;
   FFEFFECT temp;

   /* Get the effect. */
   SDL_memset(&temp, 0, sizeof(FFEFFECT));
   if (SDL_SYS_ToFFEFFECT(haptic, &temp, data) < 0) {
      goto err_update;
   }

   /* Set the flags.  Might be worthwhile to diff temp with loaded effect and
    *  only change those parameters. */
   flags = FFEP_ALLPARAMS;

   /* Create the actual effect. */
   ret = FFEffectSetParameters(effect->hweffect->ref, &temp, flags);
   if (ret != FF_OK) {
      SDL_SetError("Haptic: Unable to update effect: %s.", FFStrError(ret));
      goto err_update;
   }

   /* Copy it over. */
   SDL_SYS_HapticFreeFFEFFECT(&effect->hweffect->effect, data->type);
   SDL_memcpy(&effect->hweffect->effect, &temp, sizeof(FFEFFECT));

   return 0;

err_update:
   SDL_SYS_HapticFreeFFEFFECT(&temp, data->type);
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
      iter = FF_INFINITE;
   }
   else
      iter = iterations;

   /* Run the effect. */
   ret = FFEffectStart(effect->hweffect->ref, iter, 0);
   if (ret != FF_OK) {
      SDL_SetError("Haptic: Unable to run the effect: %s.", FFStrError(ret));
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
      SDL_SetError("Haptic: Unable to stop the effect: %s.", FFStrError(ret));
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
      SDL_SetError("Haptic: Error removing the effect from the device: %s.",
                   FFStrError(ret));
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

   ret = FFEffectGetEffectStatus(effect->hweffect->ref, &status);
   if (ret != FF_OK) {
      SDL_SetError("Haptic: Unable to get effect status: %s.", FFStrError(ret));
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
      SDL_SetError("Haptic: Error setting gain: %s.", FFStrError(ret));
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
               FFPROP_AUTOCENTER, &val);
   if (ret != FF_OK) {
      SDL_SetError("Haptic: Error setting autocenter: %s.", FFStrError(ret));
      return -1;
   }
  
   return 0;

}


#endif /* SDL_HAPTIC_IOKIT */
