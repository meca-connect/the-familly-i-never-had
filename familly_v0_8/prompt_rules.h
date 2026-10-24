#pragma once
// Prompt pools for "The Familly I Never Had". Pure C++ (no Arduino), tested on
// the host by spikes/007-night-power/test_prompt_rules.py.
//
// Identity comes from the reference image; everything else is varied on
// purpose so nightly households do not look like the same pasted face:
// scene x head pose x camera framing.
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

namespace family {

static const char* const SCENES[]={
  "a small civil wedding","a backyard wedding reception","an anniversary dinner at home",
  "breakfast in a small kitchen","cooking dinner together","unpacking boxes in a first shared home",
  "a quiet evening on the sofa","a family portrait with a spouse and children",
  "a child's birthday party at home","teaching a child to ride a bicycle",
  "reading a bedtime story to children","helping a child with homework","walking children to school",
  "a family picnic in a park","a family day at the beach","playing board games on a rainy afternoon",
  "decorating the home for a celebration","a backyard barbecue","a family camping trip",
  "a family road-trip stop","visiting parents-in-law","a gathering with siblings and their children",
  "a child's school performance","Sunday lunch with an extended family","washing dishes together"};

static const char* const POSES[]={
  "head turned three-quarters to the left, looking at someone beside them",
  "head turned three-quarters to the right, laughing at something off camera",
  "seen almost in profile, looking down at a child",
  "glancing back over one shoulder toward the camera",
  "head tilted, eyes half closed mid-laugh",
  "looking away from the camera toward the room",
  "leaning in, face lit from the side, looking at the table",
  "seen slightly from above while looking up",
  "seen from a low angle, looking down and smiling",
  "chin raised, partly turned away, listening to someone",
  "caught mid-movement, head turning toward a sound",
  "facing the camera, with an expression unlike the reference photo"};

static const char* const FRAMINGS[]={
  "a wide shot that shows the whole room",
  "a medium shot from across the table",
  "a close, intimate frame",
  "a candid snapshot from a doorway",
  "an off-centre composition with the subject not in the middle",
  "a slightly tilted handheld snapshot"};

static constexpr size_t SCENE_COUNT=sizeof(SCENES)/sizeof(SCENES[0]);
static constexpr size_t POSE_COUNT=sizeof(POSES)/sizeof(POSES[0]);
static constexpr size_t FRAMING_COUNT=sizeof(FRAMINGS)/sizeof(FRAMINGS[0]);

struct PromptChoice { unsigned scene,pose,framing; };

// Splits one random word into independent choices (device passes esp_random()).
inline uint32_t mix32(uint32_t x){x^=x>>16;x*=0x7feb352dU;x^=x>>15;x*=0x846ca68bU;x^=x>>16;return x;}
inline PromptChoice choosePrompt(uint32_t seed) {
  const uint32_t a=mix32(seed),b=mix32(seed^0x9e3779b9U),c=mix32(seed+0x632be5abU);
  return PromptChoice{unsigned(a%SCENE_COUNT),unsigned(b%POSE_COUNT),unsigned(c%FRAMING_COUNT)};
}

// Returns bytes written (excluding NUL) or 0 if the buffer is too small.
inline size_t buildPrompt(const PromptChoice& c,char* out,size_t cap) {
  if(c.scene>=SCENE_COUNT||c.pose>=POSE_COUNT||c.framing>=FRAMING_COUNT||!out||!cap)return 0;
  const int n=snprintf(out,cap,
    "The person in the reference photo is the main subject. Keep their identity, facial features "
    "and apparent age exactly; do not copy the reference photo's pose, expression, clothing, "
    "framing or background. Scene: %s, with an entirely new, invented family; everyone else is a "
    "fictional stranger. The subject's pose: %s. Camera: %s. Ordinary candid photograph, natural "
    "light, believable faces, no text, no watermark.",
    SCENES[c.scene],POSES[c.pose],FRAMINGS[c.framing]);
  if(n<0||size_t(n)>=cap)return 0;
  return size_t(n);
}

}  // namespace family
