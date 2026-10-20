#pragma once
#include "Fog.h"
#include "Environment.h"
#include "EditorVisuals.h"
// Evaluated values are ordinary copies. Neither the renderer nor evaluation writes authoring data.
struct PostProcessSettings {FogSettings fog;EnvironmentSettings environment;SceneBackground atmosphere;};
// Explicit supported fields: extend this list/state for PPV.2 rather than adding another volume evaluator.
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
 X(sunGlowIntensity,atmosphere.sunGlowIntensity,0,4,"Atmosphere","Sun Glow Intensity")
