/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "PannerNode.h"
#include "AudioNodeEngine.h"
#include "AudioNodeStream.h"
#include "AudioListener.h"
#include "AudioBufferSourceNode.h"

namespace mozilla {
namespace dom {

using namespace std;

class PannerNodeEngine : public AudioNodeEngine
{
public:
  explicit PannerNodeEngine(AudioNode* aNode)
    : AudioNodeEngine(aNode)
    // Please keep these default values consistent with PannerNode::PannerNode below.
    , mPanningModel(PanningModelTypeValues::HRTF)
    , mPanningModelFunction(&PannerNodeEngine::HRTFPanningFunction)
    , mDistanceModel(DistanceModelTypeValues::Inverse)
    , mDistanceModelFunction(&PannerNodeEngine::InverseGainFunction)
    , mPosition()
    , mOrientation(1., 0., 0.)
    , mVelocity()
    , mRefDistance(1.)
    , mMaxDistance(10000.)
    , mRolloffFactor(1.)
    , mConeInnerAngle(360.)
    , mConeOuterAngle(360.)
    , mConeOuterGain(0.)
    // These will be initialized when a PannerNode is created, so just initialize them
    // to some dummy values here.
    , mListenerDopplerFactor(0.)
    , mListenerSpeedOfSound(0.)
  {
  }

  virtual void SetInt32Parameter(uint32_t aIndex, int32_t aParam) MOZ_OVERRIDE
  {
    switch (aIndex) {
    case PannerNode::PANNING_MODEL:
      mPanningModel = PanningModelType(aParam);
      switch (mPanningModel) {
        case PanningModelTypeValues::Equalpower:
          mPanningModelFunction = &PannerNodeEngine::EqualPowerPanningFunction;
          break;
        case PanningModelTypeValues::HRTF:
          mPanningModelFunction = &PannerNodeEngine::HRTFPanningFunction;
          break;
        case PanningModelTypeValues::Soundfield:
          mPanningModelFunction = &PannerNodeEngine::SoundfieldPanningFunction;
          break;
      }
      break;
    case PannerNode::DISTANCE_MODEL:
      mDistanceModel = DistanceModelType(aParam);
      switch (mDistanceModel) {
        case DistanceModelTypeValues::Inverse:
          mDistanceModelFunction = &PannerNodeEngine::InverseGainFunction;
          break;
        case DistanceModelTypeValues::Linear:
          mDistanceModelFunction = &PannerNodeEngine::LinearGainFunction;
          break;
        case DistanceModelTypeValues::Exponential:
          mDistanceModelFunction = &PannerNodeEngine::ExponentialGainFunction;
          break;
      }
      break;
    default:
      NS_ERROR("Bad PannerNodeEngine Int32Parameter");
    }
  }
  virtual void SetThreeDPointParameter(uint32_t aIndex, const ThreeDPoint& aParam) MOZ_OVERRIDE
  {
    switch (aIndex) {
    case PannerNode::LISTENER_POSITION: mListenerPosition = aParam; break;
    case PannerNode::LISTENER_ORIENTATION: mListenerOrientation = aParam; break;
    case PannerNode::LISTENER_UPVECTOR: mListenerUpVector = aParam; break;
    case PannerNode::LISTENER_VELOCITY: mListenerVelocity = aParam; break;
    case PannerNode::POSITION: mPosition = aParam; break;
    case PannerNode::ORIENTATION: mOrientation = aParam; break;
    case PannerNode::VELOCITY: mVelocity = aParam; break;
    default:
      NS_ERROR("Bad PannerNodeEngine ThreeDPointParameter");
    }
  }
  virtual void SetDoubleParameter(uint32_t aIndex, double aParam) MOZ_OVERRIDE
  {
    switch (aIndex) {
    case PannerNode::LISTENER_DOPPLER_FACTOR: mListenerDopplerFactor = aParam; break;
    case PannerNode::LISTENER_SPEED_OF_SOUND: mListenerSpeedOfSound = aParam; break;
    case PannerNode::REF_DISTANCE: mRefDistance = aParam; break;
    case PannerNode::MAX_DISTANCE: mMaxDistance = aParam; break;
    case PannerNode::ROLLOFF_FACTOR: mRolloffFactor = aParam; break;
    case PannerNode::CONE_INNER_ANGLE: mConeInnerAngle = aParam; break;
    case PannerNode::CONE_OUTER_ANGLE: mConeOuterAngle = aParam; break;
    case PannerNode::CONE_OUTER_GAIN: mConeOuterGain = aParam; break;
    default:
      NS_ERROR("Bad PannerNodeEngine DoubleParameter");
    }
  }

  virtual void ProduceAudioBlock(AudioNodeStream* aStream,
                                 const AudioChunk& aInput,
                                 AudioChunk* aOutput,
                                 bool *aFinished) MOZ_OVERRIDE
  {
    if (aInput.IsNull()) {
      *aOutput = aInput;
      return;
    }
    (this->*mPanningModelFunction)(aInput, aOutput);
  }

  void ComputeAzimuthAndElevation(float& aAzimuth, float& aElevation);
  void DistanceAndConeGain(AudioChunk* aChunk, float aGain);
  float ComputeConeGain();

  void GainMonoToStereo(const AudioChunk& aInput, AudioChunk* aOutput,
                        float aGainL, float aGainR);
  void GainStereoToStereo(const AudioChunk& aInput, AudioChunk* aOutput,
                          float aGainL, float aGainR, double aAzimuth);

  void EqualPowerPanningFunction(const AudioChunk& aInput, AudioChunk* aOutput);
  void HRTFPanningFunction(const AudioChunk& aInput, AudioChunk* aOutput);
  void SoundfieldPanningFunction(const AudioChunk& aInput, AudioChunk* aOutput);

  float LinearGainFunction(float aDistance);
  float InverseGainFunction(float aDistance);
  float ExponentialGainFunction(float aDistance);

  PanningModelType mPanningModel;
  typedef void (PannerNodeEngine::*PanningModelFunction)(const AudioChunk& aInput, AudioChunk* aOutput);
  PanningModelFunction mPanningModelFunction;
  DistanceModelType mDistanceModel;
  typedef float (PannerNodeEngine::*DistanceModelFunction)(float aDistance);
  DistanceModelFunction mDistanceModelFunction;
  ThreeDPoint mPosition;
  ThreeDPoint mOrientation;
  ThreeDPoint mVelocity;
  double mRefDistance;
  double mMaxDistance;
  double mRolloffFactor;
  double mConeInnerAngle;
  double mConeOuterAngle;
  double mConeOuterGain;
  ThreeDPoint mListenerPosition;
  ThreeDPoint mListenerOrientation;
  ThreeDPoint mListenerUpVector;
  ThreeDPoint mListenerVelocity;
  double mListenerDopplerFactor;
  double mListenerSpeedOfSound;
};

PannerNode::PannerNode(AudioContext* aContext)
  : AudioNode(aContext)
  // Please keep these default values consistent with PannerNodeEngine::PannerNodeEngine above.
  , mPanningModel(PanningModelTypeValues::HRTF)
  , mDistanceModel(DistanceModelTypeValues::Inverse)
  , mPosition()
  , mOrientation(1., 0., 0.)
  , mVelocity()
  , mRefDistance(1.)
  , mMaxDistance(10000.)
  , mRolloffFactor(1.)
  , mConeInnerAngle(360.)
  , mConeOuterAngle(360.)
  , mConeOuterGain(0.)
{
  mStream = aContext->Graph()->CreateAudioNodeStream(new PannerNodeEngine(this),
                                                     MediaStreamGraph::INTERNAL_STREAM);
  // We should register once we have set up our stream and engine.
  Context()->Listener()->RegisterPannerNode(this);
}

PannerNode::~PannerNode()
{
  if (Context()) {
    Context()->UnregisterPannerNode(this);
  }
}

JSObject*
PannerNode::WrapObject(JSContext* aCx, JSObject* aScope)
{
  return PannerNodeBinding::Wrap(aCx, aScope, this);
}

// Those three functions are described in the spec.
float
PannerNodeEngine::LinearGainFunction(float aDistance)
{
  return 1 - mRolloffFactor * (aDistance - mRefDistance) / (mMaxDistance - mRefDistance);
}

float
PannerNodeEngine::InverseGainFunction(float aDistance)
{
  return mRefDistance / (mRefDistance + mRolloffFactor * (aDistance - mRefDistance));
}

float
PannerNodeEngine::ExponentialGainFunction(float aDistance)
{
  return pow(aDistance / mRefDistance, -mRolloffFactor);
}

void
PannerNodeEngine::SoundfieldPanningFunction(const AudioChunk& aInput,
                                            AudioChunk* aOutput)
{
  // not implemented: noop
  *aOutput = aInput;
}

void
PannerNodeEngine::HRTFPanningFunction(const AudioChunk& aInput,
                                      AudioChunk* aOutput)
{
  // not implemented: noop
  *aOutput = aInput;
}

void
PannerNodeEngine::EqualPowerPanningFunction(const AudioChunk& aInput,
                                            AudioChunk* aOutput)
{
  float azimuth, elevation, gainL, gainR, normalizedAzimuth, distance, distanceGain, coneGain;
  int inputChannels = aInput.mChannelData.Length();
  ThreeDPoint distanceVec;

  // If both the listener are in the same spot, and no cone gain is specified,
  // this node is noop.
  if (mListenerPosition == mPosition &&
      mConeInnerAngle == 360 &&
      mConeOuterAngle == 360) {
    *aOutput = aInput;
    return;
  }

  // The output of this node is always stereo, no matter what the inputs are.
  AllocateAudioBlock(2, aOutput);

  ComputeAzimuthAndElevation(azimuth, elevation);
  coneGain = ComputeConeGain();

  // The following algorithm is described in the spec.
  // Clamp azimuth in the [-90, 90] range.
  azimuth = min(180.f, max(-180.f, azimuth));

  // Wrap around
  if (azimuth < -90.f) {
    azimuth = -180.f - azimuth;
  } else if (azimuth > 90) {
    azimuth = 180.f - azimuth;
  }

  // Normalize the value in the [0, 1] range.
  if (inputChannels == 1) {
    normalizedAzimuth = (azimuth + 90.f) / 180.f;
  } else {
    if (azimuth <= 0) {
      normalizedAzimuth = (azimuth + 90.f) / 90.f;
    } else {
      normalizedAzimuth = azimuth / 90.f;
    }
  }

  // Compute how much the distance contributes to the gain reduction.
  distanceVec = mPosition - mListenerPosition;
  distance = sqrt(distanceVec.DotProduct(distanceVec));
  distanceGain = (this->*mDistanceModelFunction)(distance);

  // Actually compute the left and right gain.
  gainL = cos(0.5 * M_PI * normalizedAzimuth) * aInput.mVolume;
  gainR = sin(0.5 * M_PI * normalizedAzimuth) * aInput.mVolume;

  // Compute the output.
  if (inputChannels == 1) {
    GainMonoToStereo(aInput, aOutput, gainL, gainR);
  } else {
    GainStereoToStereo(aInput, aOutput, gainL, gainR, azimuth);
  }

  DistanceAndConeGain(aOutput, distanceGain * coneGain);
}

void
PannerNodeEngine::GainMonoToStereo(const AudioChunk& aInput, AudioChunk* aOutput,
                                   float aGainL, float aGainR)
{
  float* outputL = static_cast<float*>(const_cast<void*>(aOutput->mChannelData[0]));
  float* outputR = static_cast<float*>(const_cast<void*>(aOutput->mChannelData[1]));
  const float* input = static_cast<float*>(const_cast<void*>(aInput.mChannelData[0]));

  AudioBlockPanMonoToStereo(input, aGainL, aGainR, outputL, outputR);
}

void
PannerNodeEngine::GainStereoToStereo(const AudioChunk& aInput, AudioChunk* aOutput,
                                     float aGainL, float aGainR, double aAzimuth)
{
  float* outputL = static_cast<float*>(const_cast<void*>(aOutput->mChannelData[0]));
  float* outputR = static_cast<float*>(const_cast<void*>(aOutput->mChannelData[1]));
  const float* inputL = static_cast<float*>(const_cast<void*>(aInput.mChannelData[0]));
  const float* inputR = static_cast<float*>(const_cast<void*>(aInput.mChannelData[1]));

  AudioBlockPanStereoToStereo(inputL, inputR, aGainL, aGainR, aAzimuth <= 0, outputL, outputR);
}

void
PannerNodeEngine::DistanceAndConeGain(AudioChunk* aChunk, float aGain)
{
  float* samples = static_cast<float*>(const_cast<void*>(*aChunk->mChannelData.Elements()));
  uint32_t channelCount = aChunk->mChannelData.Length();

  AudioBlockInPlaceScale(samples, channelCount, aGain);
}

// This algorithm is specicied in the webaudio spec.
void
PannerNodeEngine::ComputeAzimuthAndElevation(float& aAzimuth, float& aElevation)
{
  ThreeDPoint sourceListener = mPosition - mListenerPosition;

  if (sourceListener.IsZero()) {
    aAzimuth = 0.0;
    aElevation = 0.0;
    return;
  }

  sourceListener.Normalize();

  // Project the source-listener vector on the x-z plane.
  ThreeDPoint& listenerFront = mListenerOrientation;
  ThreeDPoint listenerRightNorm = listenerFront.CrossProduct(mListenerUpVector);
  listenerRightNorm.Normalize();

  ThreeDPoint listenerFrontNorm(listenerFront);
  listenerFrontNorm.Normalize();

  ThreeDPoint up = listenerRightNorm.CrossProduct(listenerFrontNorm);

  double upProjection = sourceListener.DotProduct(up);

  ThreeDPoint projectedSource = sourceListener - up * upProjection;
  projectedSource.Normalize();

  // Actually compute the angle, and convert to degrees
  double projection = projectedSource.DotProduct(listenerRightNorm);
  aAzimuth = 180 * acos(projection) / M_PI;

  // Compute whether the source is in front or behind the listener.
  double frontBack = projectedSource.DotProduct(listenerFrontNorm);
  if (frontBack < 0) {
    aAzimuth = 360 - aAzimuth;
  }
  // Rotate the azimuth so it is relative to the listener front vector instead
  // of the right vector.
  if ((aAzimuth >= 0) && (aAzimuth <= 270)) {
    aAzimuth = 90 - aAzimuth;
  } else {
    aAzimuth = 450 - aAzimuth;
  }

  aElevation = 90 - 180 * acos(sourceListener.DotProduct(up)) / M_PI;

  if (aElevation > 90) {
    aElevation = 180 - aElevation;
  } else if (aElevation < -90) {
    aElevation = -180 - aElevation;
  }
}

// This algorithm is described in the WebAudio spec.
float
PannerNodeEngine::ComputeConeGain()
{
  // Omnidirectional source
  if (mOrientation.IsZero() || ((mConeInnerAngle == 360) && (mConeOuterAngle == 360))) {
    return 1;
  }

  // Normalized source-listener vector
  ThreeDPoint sourceToListener = mListenerPosition - mPosition;
  sourceToListener.Normalize();

  ThreeDPoint normalizedSourceOrientation = mOrientation;
  normalizedSourceOrientation.Normalize();

  // Angle between the source orientation vector and the source-listener vector
  double dotProduct = sourceToListener.DotProduct(normalizedSourceOrientation);
  double angle = 180 * acos(dotProduct) / M_PI;
  double absAngle = fabs(angle);

  // Divide by 2 here since API is entire angle (not half-angle)
  double absInnerAngle = fabs(mConeInnerAngle) / 2;
  double absOuterAngle = fabs(mConeOuterAngle) / 2;
  double gain = 1;

  if (absAngle <= absInnerAngle) {
    // No attenuation
    gain = 1;
  } else if (absAngle >= absOuterAngle) {
    // Max attenuation
    gain = mConeOuterGain;
  } else {
    // Between inner and outer cones
    // inner -> outer, x goes from 0 -> 1
    double x = (absAngle - absInnerAngle) / (absOuterAngle - absInnerAngle);
    gain = (1 - x) + mConeOuterGain * x;
  }

  return gain;
}

float
PannerNode::ComputeDopplerShift()
{
  double dopplerShift = 1.0; // Initialize to default value

  AudioListener* listener = Context()->Listener();

  if (listener->DopplerFactor() > 0) {
    // Don't bother if both source and listener have no velocity.
    if (!mVelocity.IsZero() || !listener->Velocity().IsZero()) {
      // Calculate the source to listener vector.
      ThreeDPoint sourceToListener = mPosition - listener->Velocity();

      double sourceListenerMagnitude = sourceToListener.Magnitude();

      double listenerProjection = sourceToListener.DotProduct(listener->Velocity()) / sourceListenerMagnitude;
      double sourceProjection = sourceToListener.DotProduct(mVelocity) / sourceListenerMagnitude;

      listenerProjection = -listenerProjection;
      sourceProjection = -sourceProjection;

      double scaledSpeedOfSound = listener->DopplerFactor() / listener->DopplerFactor();
      listenerProjection = min(listenerProjection, scaledSpeedOfSound);
      sourceProjection = min(sourceProjection, scaledSpeedOfSound);

      dopplerShift = ((listener->SpeedOfSound() - listener->DopplerFactor() * listenerProjection) / (listener->SpeedOfSound() - listener->DopplerFactor() * sourceProjection));

      WebAudioUtils::FixNaN(dopplerShift); // Avoid illegal values

      // Limit the pitch shifting to 4 octaves up and 3 octaves down.
      dopplerShift = min(dopplerShift, 16.);
      dopplerShift = max(dopplerShift, 0.125);
    }
  }

  return dopplerShift;
}

void
PannerNode::FindConnectedSources()
{
  mSources.Clear();
  std::set<AudioNode*> cycleSet;
  FindConnectedSources(this, mSources, cycleSet);
  for (unsigned i = 0; i < mSources.Length(); i++) {
    mSources[i]->RegisterPannerNode(this);
  }
}

void
PannerNode::FindConnectedSources(AudioNode* aNode,
                                 nsTArray<AudioBufferSourceNode*>& aSources,
                                 std::set<AudioNode*>& aNodesSeen)
{
  if (!aNode) {
    return;
  }

  const nsTArray<InputNode>& inputNodes = aNode->InputNodes();

  for(unsigned i = 0; i < inputNodes.Length(); i++) {
    // Return if we find a node that we have seen already.
    if (aNodesSeen.find(inputNodes[i].mInputNode) != aNodesSeen.end()) {
      return;
    }
    aNodesSeen.insert(inputNodes[i].mInputNode);
    // Recurse
    FindConnectedSources(inputNodes[i].mInputNode, aSources, aNodesSeen);

    // Check if this node is an AudioBufferSourceNode
    AudioBufferSourceNode* node = inputNodes[i].mInputNode->AsAudioBufferSourceNode();
    if (node) {
      aSources.AppendElement(node);
    }
  }
}

void
PannerNode::SendDopplerToSourcesIfNeeded()
{
  // Don't bother sending the doppler shift if both the source and the listener
  // are not moving, because the doppler shift is going to be 1.0.
  if (!(Context()->Listener()->Velocity().IsZero() && mVelocity.IsZero())) {
    for(uint32_t i = 0; i < mSources.Length(); i++) {
      mSources[i]->SendDopplerShiftToStream(ComputeDopplerShift());
    }
  }
}


}
}

