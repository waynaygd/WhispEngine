#pragma once
#include "Fog.h"
#include "ScreenEffects.h"
#include "Environment.h"
#include "EditorVisuals.h"
// Evaluated values are ordinary copies. Neither the renderer nor evaluation writes authoring data.
struct PostProcessSettings {FogSettings fog;EnvironmentSettings environment;SceneBackground atmosphere;ScreenEffects screenEffects;};
// One field list drives overrides, metadata, validation and the shared volume blend.
#define WHISP_PP_FLOATS(X) \
 X(fogDensity,fog.density,0,1,"Fog","Intensity") \
 X(fogStart,fog.startDistance,0,10000,"Fog","Start Distance") \
 X(fogEnd,fog.endDistance,.001f,10000,"Fog","End Distance") \
 X(fogBaseHeight,fog.baseHeight,-10000,10000,"Height Fog","Base Height") \
 X(heightDensity,fog.heightDensity,0,10,"Height Fog","Height Density") \
 X(heightFalloff,fog.heightFalloff,0,10,"Height Fog","Height Falloff") \
 X(extinction,fog.extinction,0,10,"Volumetric Fog","Extinction") \
 X(anisotropy,fog.anisotropy,-.95f,.95f,"Volumetric Fog","Anisotropy") \
 X(fogMaxDistance,fog.maxDistance,.01f,10000,"Volumetric Fog","Max Distance / Sky") \
 X(environmentIntensity,environment.intensity,0,10000,"Environment","IBL Intensity") \
 X(exposure,environment.exposure,-16,16,"Environment","IBL Exposure") \
 X(environmentRotation,environment.rotationDegrees,-36000,36000,"Environment","IBL Rotation (degrees)") \
 X(skyIntensity,atmosphere.skyIntensity,0,32,"Atmosphere","Sky Intensity") \
 X(horizonGlow,atmosphere.horizonGlow,0,8,"Atmosphere","Horizon Glow") \
 X(sunsetWarmth,atmosphere.sunsetWarmth,0,4,"Atmosphere","Sunset Warmth") \
 X(nightIntensity,atmosphere.nightIntensity,0,4,"Atmosphere","Night Intensity") \
 X(sunIntensity,atmosphere.sunIntensity,0,32,"Atmosphere","Sun Disc Intensity") \
 X(sunGlowIntensity,atmosphere.sunGlowIntensity,0,4,"Atmosphere","Sun Glow Intensity") \
 X(saturation,screenEffects.saturation,0,2,"Color Grading","Saturation") \
 X(contrast,screenEffects.contrast,0,2,"Color Grading","Contrast") \
 X(brightness,screenEffects.brightness,-1,1,"Color Grading","Brightness") \
 X(vignetteIntensity,screenEffects.vignetteIntensity,0,1,"Vignette","Intensity") \
 X(vignetteSmoothness,screenEffects.vignetteSmoothness,.01f,1,"Vignette","Smoothness") \
 X(vignetteRoundness,screenEffects.vignetteRoundness,0,1,"Vignette","Roundness") \
 X(grainIntensity,screenEffects.grainIntensity,0,1,"Film Grain","Intensity") \
 X(grainScale,screenEffects.grainScale,.5f,4,"Film Grain","Scale (pixels)") \
 X(aberrationIntensity,screenEffects.aberrationIntensity,0,1,"Chromatic Aberration","Intensity") \
 X(aberrationFalloff,screenEffects.aberrationFalloff,.1f,8,"Chromatic Aberration","Radial Falloff")
