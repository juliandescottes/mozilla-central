/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SpeechRecognition.h"

#include "nsCOMPtr.h"
#include "nsContentUtils.h"
#include "nsCycleCollectionParticipant.h"

#include "mozilla/dom/SpeechRecognitionBinding.h"

#include "AudioSegment.h"
#include "endpointer.h"

#include "GeneratedEvents.h"
#include "nsIDOMSpeechRecognitionEvent.h"

#include <algorithm>

namespace mozilla {
namespace dom {

#define PREFERENCE_DEFAULT_RECOGNITION_SERVICE "media.webspeech.service.default"
#define DEFAULT_RECOGNITION_SERVICE "google"

#define PREFERENCE_ENDPOINTER_SILENCE_LENGTH "media.webspeech.silence_length"
#define PREFERENCE_ENDPOINTER_LONG_SILENCE_LENGTH "media.webspeech.long_silence_length"
#define PREFERENCE_ENDPOINTER_LONG_SPEECH_LENGTH "media.webspeech.long_speech_length"

static const uint32_t kSAMPLE_RATE = 16000;
static const uint32_t kSPEECH_DETECTION_TIMEOUT_MS = 10000;

// number of frames corresponding to 300ms of audio to send to endpointer while
// it's in environment estimation mode
// kSAMPLE_RATE frames = 1s, kESTIMATION_FRAMES frames = 300ms
static const uint32_t kESTIMATION_SAMPLES = 300 * kSAMPLE_RATE / 1000;

#ifdef PR_LOGGING
PRLogModuleInfo*
GetSpeechRecognitionLog()
{
  static PRLogModuleInfo* sLog;
  if (!sLog) {
    sLog = PR_NewLogModule("SpeechRecognition");
  }

  return sLog;
}
#define SR_LOG(...) PR_LOG(GetSpeechRecognitionLog(), PR_LOG_DEBUG, (__VA_ARGS__))
#else
#define SR_LOG(...)
#endif

NS_INTERFACE_MAP_BEGIN(SpeechRecognition)
  NS_INTERFACE_MAP_ENTRY(nsIObserver)
NS_INTERFACE_MAP_END_INHERITING(nsDOMEventTargetHelper)

NS_IMPL_ADDREF_INHERITED(SpeechRecognition, nsDOMEventTargetHelper)
NS_IMPL_RELEASE_INHERITED(SpeechRecognition, nsDOMEventTargetHelper)

struct SpeechRecognition::TestConfig SpeechRecognition::mTestConfig;

SpeechRecognition::SpeechRecognition()
  : mEndpointer(kSAMPLE_RATE)
  , mAudioSamplesPerChunk(mEndpointer.FrameSize())
  , mSpeechDetectionTimer(do_CreateInstance(NS_TIMER_CONTRACTID))
{
  SR_LOG("created SpeechRecognition");
  SetIsDOMBinding();

  mTestConfig.Init();
  if (mTestConfig.mEnableTests) {
    nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
    obs->AddObserver(this, SPEECH_RECOGNITION_TEST_EVENT_REQUEST_TOPIC, false);
    obs->AddObserver(this, SPEECH_RECOGNITION_TEST_END_TOPIC, false);
  }

  mEndpointer.set_speech_input_complete_silence_length(
      Preferences::GetInt(PREFERENCE_ENDPOINTER_SILENCE_LENGTH, 500000));
  mEndpointer.set_long_speech_input_complete_silence_length(
      Preferences::GetInt(PREFERENCE_ENDPOINTER_LONG_SILENCE_LENGTH, 1000000));
  mEndpointer.set_long_speech_length(
      Preferences::GetInt(PREFERENCE_ENDPOINTER_SILENCE_LENGTH, 3 * 1000000));
  Reset();
}

bool
SpeechRecognition::StateBetween(FSMState begin, FSMState end)
{
  return mCurrentState >= begin && mCurrentState <= end;
}

void
SpeechRecognition::SetState(FSMState state)
{
  mCurrentState = state;
  SR_LOG("Transitioned to state %s", GetName(mCurrentState));
  return;
}

JSObject*
SpeechRecognition::WrapObject(JSContext* aCx, JSObject* aScope)
{
  return SpeechRecognitionBinding::Wrap(aCx, aScope, this);
}

already_AddRefed<SpeechRecognition>
SpeechRecognition::Constructor(const GlobalObject& aGlobal, ErrorResult& aRv)
{
  nsCOMPtr<nsPIDOMWindow> win = do_QueryInterface(aGlobal.Get());
  if (!win) {
    aRv.Throw(NS_ERROR_FAILURE);
  }

  MOZ_ASSERT(win->IsInnerWindow());
  nsRefPtr<SpeechRecognition> object = new SpeechRecognition();
  object->BindToOwner(win);
  return object.forget();
}

nsISupports*
SpeechRecognition::GetParentObject() const
{
  return GetOwner();
}

void
SpeechRecognition::ProcessEvent(SpeechEvent* aEvent)
{
  SR_LOG("Processing %s, current state is %s",
         GetName(aEvent),
         GetName(mCurrentState));

  // Run priority events first
  for (uint32_t i = 0; i < mPriorityEvents.Length(); ++i) {
    nsRefPtr<SpeechEvent> event = mPriorityEvents[i];

    SR_LOG("Processing priority %s", GetName(event));
    Transition(event);
  }

  mPriorityEvents.Clear();

  SR_LOG("Processing %s received as argument", GetName(aEvent));
  Transition(aEvent);
}

void
SpeechRecognition::Transition(SpeechEvent* aEvent)
{
  switch (mCurrentState) {
    case STATE_IDLE:
      switch (aEvent->mType) {
        case EVENT_START:
          // TODO: may want to time out if we wait too long
          // for user to approve
          WaitForAudioData(aEvent);
          break;
        case EVENT_STOP:
        case EVENT_ABORT:
        case EVENT_AUDIO_DATA:
        case EVENT_RECOGNITIONSERVICE_INTERMEDIATE_RESULT:
        case EVENT_RECOGNITIONSERVICE_FINAL_RESULT:
          DoNothing(aEvent);
          break;
        case EVENT_AUDIO_ERROR:
        case EVENT_RECOGNITIONSERVICE_ERROR:
          AbortError(aEvent);
          break;
        case EVENT_COUNT:
          MOZ_NOT_REACHED("Invalid event EVENT_COUNT");
      }
      break;
    case STATE_STARTING:
      switch (aEvent->mType) {
        case EVENT_AUDIO_DATA:
          StartedAudioCapture(aEvent);
          break;
        case EVENT_AUDIO_ERROR:
        case EVENT_RECOGNITIONSERVICE_ERROR:
          AbortError(aEvent);
          break;
        case EVENT_ABORT:
          AbortSilently(aEvent);
          break;
        case EVENT_STOP:
          Reset();
          break;
        case EVENT_RECOGNITIONSERVICE_INTERMEDIATE_RESULT:
        case EVENT_RECOGNITIONSERVICE_FINAL_RESULT:
          DoNothing(aEvent);
          break;
        case EVENT_START:
          SR_LOG("STATE_STARTING: Unhandled event %s", GetName(aEvent));
          MOZ_NOT_REACHED("");
        case EVENT_COUNT:
          MOZ_NOT_REACHED("Invalid event EVENT_COUNT");
      }
      break;
    case STATE_ESTIMATING:
      switch (aEvent->mType) {
        case EVENT_AUDIO_DATA:
          WaitForEstimation(aEvent);
          break;
        case EVENT_STOP:
          StopRecordingAndRecognize(aEvent);
          break;
        case EVENT_ABORT:
          AbortSilently(aEvent);
          break;
        case EVENT_RECOGNITIONSERVICE_INTERMEDIATE_RESULT:
        case EVENT_RECOGNITIONSERVICE_FINAL_RESULT:
        case EVENT_RECOGNITIONSERVICE_ERROR:
          DoNothing(aEvent);
          break;
        case EVENT_AUDIO_ERROR:
          AbortError(aEvent);
          break;
        case EVENT_START:
          SR_LOG("STATE_ESTIMATING: Unhandled event %d", aEvent->mType);
          MOZ_NOT_REACHED("");
        case EVENT_COUNT:
          MOZ_NOT_REACHED("Invalid event EVENT_COUNT");
      }
      break;
    case STATE_WAITING_FOR_SPEECH:
      switch (aEvent->mType) {
        case EVENT_AUDIO_DATA:
          DetectSpeech(aEvent);
          break;
        case EVENT_STOP:
          StopRecordingAndRecognize(aEvent);
          break;
        case EVENT_ABORT:
          AbortSilently(aEvent);
          break;
        case EVENT_AUDIO_ERROR:
          AbortError(aEvent);
          break;
        case EVENT_RECOGNITIONSERVICE_INTERMEDIATE_RESULT:
        case EVENT_RECOGNITIONSERVICE_FINAL_RESULT:
        case EVENT_RECOGNITIONSERVICE_ERROR:
          DoNothing(aEvent);
          break;
        case EVENT_START:
          SR_LOG("STATE_STARTING: Unhandled event %s", GetName(aEvent));
          MOZ_NOT_REACHED("");
        case EVENT_COUNT:
          MOZ_NOT_REACHED("Invalid event EVENT_COUNT");
      }
      break;
    case STATE_RECOGNIZING:
      switch (aEvent->mType) {
        case EVENT_AUDIO_DATA:
          WaitForSpeechEnd(aEvent);
          break;
        case EVENT_STOP:
          StopRecordingAndRecognize(aEvent);
          break;
        case EVENT_AUDIO_ERROR:
        case EVENT_RECOGNITIONSERVICE_ERROR:
          AbortError(aEvent);
          break;
        case EVENT_ABORT:
          AbortSilently(aEvent);
          break;
        case EVENT_RECOGNITIONSERVICE_FINAL_RESULT:
        case EVENT_RECOGNITIONSERVICE_INTERMEDIATE_RESULT:
          DoNothing(aEvent);
          break;
        case EVENT_START:
          SR_LOG("STATE_RECOGNIZING: Unhandled aEvent %s", GetName(aEvent));
          MOZ_NOT_REACHED("");
        case EVENT_COUNT:
          MOZ_NOT_REACHED("Invalid event EVENT_COUNT");
      }
      break;
    case STATE_WAITING_FOR_RESULT:
      switch (aEvent->mType) {
        case EVENT_STOP:
          DoNothing(aEvent);
          break;
        case EVENT_AUDIO_ERROR:
        case EVENT_RECOGNITIONSERVICE_ERROR:
          AbortError(aEvent);
          break;
        case EVENT_RECOGNITIONSERVICE_FINAL_RESULT:
          NotifyFinalResult(aEvent);
          break;
        case EVENT_AUDIO_DATA:
          DoNothing(aEvent);
          break;
        case EVENT_ABORT:
          AbortSilently(aEvent);
          break;
        case EVENT_START:
        case EVENT_RECOGNITIONSERVICE_INTERMEDIATE_RESULT:
          SR_LOG("STATE_WAITING_FOR_RESULT: Unhandled aEvent %s", GetName(aEvent));
          MOZ_NOT_REACHED("");
        case EVENT_COUNT:
          MOZ_NOT_REACHED("Invalid event EVENT_COUNT");
      }
      break;
    case STATE_ABORTING:
      DoNothing(aEvent);
      break;
    case STATE_COUNT:
      MOZ_NOT_REACHED("Invalid state STATE_COUNT");
  }

  return;
}

/*
 * Handle a segment of recorded audio data.
 * Returns the number of samples that were processed.
 */
uint32_t
SpeechRecognition::ProcessAudioSegment(AudioSegment* aSegment)
{
  AudioSegment::ChunkIterator iterator(*aSegment);
  uint32_t samples = 0;
  while (!iterator.IsEnded()) {
    float out;
    mEndpointer.ProcessAudio(*iterator, &out);
    samples += iterator->GetDuration();
    iterator.Next();
  }

  mRecognitionService->ProcessAudioSegment(aSegment);
  return samples;
}

void
SpeechRecognition::GetRecognitionServiceCID(nsACString& aResultCID)
{
  if (mTestConfig.mFakeRecognitionService) {
    aResultCID =
      NS_SPEECH_RECOGNITION_SERVICE_CONTRACTID_PREFIX "fake";

    return;
  }

  nsAdoptingCString prefValue =
    Preferences::GetCString(PREFERENCE_DEFAULT_RECOGNITION_SERVICE);

  nsAutoCString speechRecognitionService;
  if (!prefValue.get() || prefValue.IsEmpty()) {
    speechRecognitionService = DEFAULT_RECOGNITION_SERVICE;
  } else {
    speechRecognitionService = prefValue;
  }

  aResultCID =
    NS_LITERAL_CSTRING(NS_SPEECH_RECOGNITION_SERVICE_CONTRACTID_PREFIX) +
    speechRecognitionService;

  return;
}

/****************************************************************************
 * FSM Transition functions
 *
 * If a transition function may cause a DOM event to be fired,
 * it may also be re-entered, since the event handler may cause the
 * event loop to spin and new SpeechEvents to be processed.
 *
 * Rules:
 * 1) These methods should call SetState as soon as possible.
 * 2) If these methods dispatch DOM events, or call methods that dispatch
 * DOM events, that should be done as late as possible.
 * 3) If anything must happen after dispatching a DOM event, make sure
 * the state is still what the method expected it to be.
 ****************************************************************************/

void
SpeechRecognition::Reset()
{
  SetState(STATE_IDLE);
  mRecognitionService = nullptr;
  mEstimationSamples = 0;
  mBufferedSamples = 0;
  mSpeechDetectionTimer->Cancel();
}

void
SpeechRecognition::ResetAndEnd()
{
  Reset();
  DispatchTrustedEvent(NS_LITERAL_STRING("end"));
}

void
SpeechRecognition::WaitForAudioData(SpeechEvent* aEvent)
{
  SetState(STATE_STARTING);
}

void
SpeechRecognition::StartedAudioCapture(SpeechEvent* aEvent)
{
  SetState(STATE_ESTIMATING);

  mEndpointer.SetEnvironmentEstimationMode();
  mEstimationSamples += ProcessAudioSegment(aEvent->mAudioSegment);

  DispatchTrustedEvent(NS_LITERAL_STRING("audiostart"));
  if (mCurrentState == STATE_ESTIMATING) {
    DispatchTrustedEvent(NS_LITERAL_STRING("start"));
  }
}

void
SpeechRecognition::StopRecordingAndRecognize(SpeechEvent* aEvent)
{
  SetState(STATE_WAITING_FOR_RESULT);

  MOZ_ASSERT(mRecognitionService, "Service deleted before recording done");
  mRecognitionService->SoundEnd();

  StopRecording();
}

void
SpeechRecognition::WaitForEstimation(SpeechEvent* aEvent)
{
  SetState(STATE_ESTIMATING);

  mEstimationSamples += ProcessAudioSegment(aEvent->mAudioSegment);
  if (mEstimationSamples > kESTIMATION_SAMPLES) {
    mEndpointer.SetUserInputMode();
    SetState(STATE_WAITING_FOR_SPEECH);
  }
}

void
SpeechRecognition::DetectSpeech(SpeechEvent* aEvent)
{
  SetState(STATE_WAITING_FOR_SPEECH);

  ProcessAudioSegment(aEvent->mAudioSegment);
  if (mEndpointer.DidStartReceivingSpeech()) {
    mSpeechDetectionTimer->Cancel();
    SetState(STATE_RECOGNIZING);
    DispatchTrustedEvent(NS_LITERAL_STRING("speechstart"));
  }
}

void
SpeechRecognition::WaitForSpeechEnd(SpeechEvent* aEvent)
{
  SetState(STATE_RECOGNIZING);

  ProcessAudioSegment(aEvent->mAudioSegment);
  if (mEndpointer.speech_input_complete()) {
    DispatchTrustedEvent(NS_LITERAL_STRING("speechend"));

    if (mCurrentState == STATE_RECOGNIZING) {
      // FIXME: StopRecordingAndRecognize should only be called for single
      // shot services for continuous we should just inform the service
      StopRecordingAndRecognize(aEvent);
    }
  }
}

void
SpeechRecognition::NotifyFinalResult(SpeechEvent* aEvent)
{
  ResetAndEnd();

  nsCOMPtr<nsIDOMEvent> domEvent;
  NS_NewDOMSpeechRecognitionEvent(getter_AddRefs(domEvent), nullptr, nullptr, nullptr);

  nsCOMPtr<nsIDOMSpeechRecognitionEvent> srEvent = do_QueryInterface(domEvent);
  nsRefPtr<SpeechRecognitionResultList> rlist = aEvent->mRecognitionResultList;
  nsCOMPtr<nsISupports> ilist = do_QueryInterface(rlist);
  srEvent->InitSpeechRecognitionEvent(NS_LITERAL_STRING("result"),
                                      true, false, 0, ilist,
                                      NS_LITERAL_STRING("NOT_IMPLEMENTED"),
                                      NULL);
  domEvent->SetTrusted(true);

  bool defaultActionEnabled;
  this->DispatchEvent(domEvent, &defaultActionEnabled);
}

void
SpeechRecognition::DoNothing(SpeechEvent* aEvent)
{
}

void
SpeechRecognition::AbortSilently(SpeechEvent* aEvent)
{
  bool stopRecording = StateBetween(STATE_ESTIMATING, STATE_RECOGNIZING);

  // prevent reentrancy from DOM events
  SetState(STATE_ABORTING);

  if (mRecognitionService) {
    mRecognitionService->Abort();
  }

  if (stopRecording) {
    StopRecording();
  }

  ResetAndEnd();
}

void
SpeechRecognition::AbortError(SpeechEvent* aEvent)
{
  AbortSilently(aEvent);
  NotifyError(aEvent);
}

void
SpeechRecognition::NotifyError(SpeechEvent* aEvent)
{
  nsCOMPtr<nsIDOMEvent> domEvent = do_QueryInterface(aEvent->mError);
  domEvent->SetTrusted(true);

  bool defaultActionEnabled;
  this->DispatchEvent(domEvent, &defaultActionEnabled);

  return;
}

/**************************************
 * Event triggers and other functions *
 **************************************/
NS_IMETHODIMP
SpeechRecognition::StartRecording(DOMMediaStream* aDOMStream)
{
  // hold a reference so that the underlying stream
  // doesn't get Destroy()'ed
  mDOMStream = aDOMStream;

  NS_ENSURE_STATE(mDOMStream->GetStream());
  mSpeechListener = new SpeechStreamListener(this);
  mDOMStream->GetStream()->AddListener(mSpeechListener);

  mEndpointer.StartSession();

  return mSpeechDetectionTimer->Init(this, kSPEECH_DETECTION_TIMEOUT_MS,
                                     nsITimer::TYPE_ONE_SHOT);
}

NS_IMETHODIMP
SpeechRecognition::StopRecording()
{
  // we only really need to remove the listener explicitly when testing,
  // as our JS code still holds a reference to mDOMStream and only assigning
  // it to nullptr isn't guaranteed to free the stream and the listener.
  mDOMStream->GetStream()->RemoveListener(mSpeechListener);
  mSpeechListener = nullptr;
  mDOMStream = nullptr;

  mEndpointer.EndSession();
  DispatchTrustedEvent(NS_LITERAL_STRING("audioend"));

  return NS_OK;
}

NS_IMETHODIMP
SpeechRecognition::Observe(nsISupports* aSubject, const char* aTopic,
                           const PRUnichar* aData)
{
  MOZ_ASSERT(NS_IsMainThread(), "Observer invoked off the main thread");

  if (!strcmp(aTopic, NS_TIMER_CALLBACK_TOPIC) &&
      StateBetween(STATE_IDLE, STATE_WAITING_FOR_SPEECH)) {

    DispatchError(SpeechRecognition::EVENT_AUDIO_ERROR,
                  nsIDOMSpeechRecognitionError::NO_SPEECH,
                  NS_LITERAL_STRING("No speech detected (timeout)"));
  } else if (!strcmp(aTopic, SPEECH_RECOGNITION_TEST_END_TOPIC)) {
    nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
    obs->RemoveObserver(this, SPEECH_RECOGNITION_TEST_EVENT_REQUEST_TOPIC);
    obs->RemoveObserver(this, SPEECH_RECOGNITION_TEST_END_TOPIC);
  } else if (mTestConfig.mFakeFSMEvents &&
             !strcmp(aTopic, SPEECH_RECOGNITION_TEST_EVENT_REQUEST_TOPIC)) {
    ProcessTestEventRequest(aSubject, nsDependentString(aData));
  }

  return NS_OK;
}

void
SpeechRecognition::ProcessTestEventRequest(nsISupports* aSubject, const nsAString& aEventName)
{
  if (aEventName.EqualsLiteral("EVENT_START")) {
    ErrorResult err;
    Start(err);
  } else if (aEventName.EqualsLiteral("EVENT_STOP")) {
    Stop();
  } else if (aEventName.EqualsLiteral("EVENT_ABORT")) {
    Abort();
  } else if (aEventName.EqualsLiteral("EVENT_AUDIO_ERROR")) {
    DispatchError(SpeechRecognition::EVENT_AUDIO_ERROR,
                  nsIDOMSpeechRecognitionError::AUDIO_CAPTURE, // TODO different codes?
                  NS_LITERAL_STRING("AUDIO_ERROR test event"));
  } else if (aEventName.EqualsLiteral("EVENT_AUDIO_DATA")) {
    StartRecording(static_cast<DOMMediaStream*>(aSubject));
  } else {
    NS_ASSERTION(mTestConfig.mFakeRecognitionService,
                 "Got request for fake recognition service event, but "
                 TEST_PREFERENCE_FAKE_RECOGNITION_SERVICE " is unset");

    // let the fake recognition service handle the request
  }

  return;
}

already_AddRefed<SpeechGrammarList>
SpeechRecognition::GetGrammars(ErrorResult& aRv) const
{
  aRv.Throw(NS_ERROR_NOT_IMPLEMENTED);
  return nullptr;
}

void
SpeechRecognition::SetGrammars(mozilla::dom::SpeechGrammarList& aArg,
                               ErrorResult& aRv)
{
  aRv.Throw(NS_ERROR_NOT_IMPLEMENTED);
  return;
}

void
SpeechRecognition::GetLang(nsString& aRetVal, ErrorResult& aRv) const
{
  aRv.Throw(NS_ERROR_NOT_IMPLEMENTED);
  return;
}

void
SpeechRecognition::SetLang(const nsAString& aArg, ErrorResult& aRv)
{
  aRv.Throw(NS_ERROR_NOT_IMPLEMENTED);
  return;
}

bool
SpeechRecognition::GetContinuous(ErrorResult& aRv) const
{
  aRv.Throw(NS_ERROR_NOT_IMPLEMENTED);
  return false;
}

void
SpeechRecognition::SetContinuous(bool aArg, ErrorResult& aRv)
{
  aRv.Throw(NS_ERROR_NOT_IMPLEMENTED);
  return;
}

bool
SpeechRecognition::GetInterimResults(ErrorResult& aRv) const
{
  aRv.Throw(NS_ERROR_NOT_IMPLEMENTED);
  return false;
}

void
SpeechRecognition::SetInterimResults(bool aArg, ErrorResult& aRv)
{
  aRv.Throw(NS_ERROR_NOT_IMPLEMENTED);
  return;
}

uint32_t
SpeechRecognition::GetMaxAlternatives(ErrorResult& aRv) const
{
  aRv.Throw(NS_ERROR_NOT_IMPLEMENTED);
  return 0;
}

void
SpeechRecognition::SetMaxAlternatives(uint32_t aArg, ErrorResult& aRv)
{
  aRv.Throw(NS_ERROR_NOT_IMPLEMENTED);
  return;
}

void
SpeechRecognition::GetServiceURI(nsString& aRetVal, ErrorResult& aRv) const
{
  aRv.Throw(NS_ERROR_NOT_IMPLEMENTED);
  return;
}

void
SpeechRecognition::SetServiceURI(const nsAString& aArg, ErrorResult& aRv)
{
  aRv.Throw(NS_ERROR_NOT_IMPLEMENTED);
  return;
}

void
SpeechRecognition::Start(ErrorResult& aRv)
{
  if (!mCurrentState == STATE_IDLE) {
    aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    return;
  }

  nsAutoCString speechRecognitionServiceCID;
  GetRecognitionServiceCID(speechRecognitionServiceCID);

  nsresult rv;
  mRecognitionService = do_GetService(speechRecognitionServiceCID.get(), &rv);
  NS_ENSURE_SUCCESS_VOID(rv);

  rv = mRecognitionService->Initialize(this->asWeakPtr());
  NS_ENSURE_SUCCESS_VOID(rv);

  if (!mTestConfig.mFakeFSMEvents) {
    MediaManager* manager = MediaManager::Get();
    manager->GetUserMedia(false,
                          GetOwner(),
                          new GetUserMediaStreamOptions(),
                          new GetUserMediaSuccessCallback(this),
                          new GetUserMediaErrorCallback(this));
  }

  nsRefPtr<SpeechEvent> event = new SpeechEvent(this, EVENT_START);
  NS_DispatchToMainThread(event);
}

void
SpeechRecognition::Stop()
{
  nsRefPtr<SpeechEvent> event = new SpeechEvent(this, EVENT_STOP);
  NS_DispatchToMainThread(event);
}

void
SpeechRecognition::Abort()
{
  nsRefPtr<SpeechEvent> event = new SpeechEvent(this, EVENT_ABORT);
  NS_DispatchToMainThread(event);
}

void
SpeechRecognition::DispatchError(EventType aErrorType, int aErrorCode,
                                 const nsAString& aMessage)
{
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aErrorType == EVENT_RECOGNITIONSERVICE_ERROR ||
             aErrorType == EVENT_AUDIO_ERROR, "Invalid error type!");

  nsCOMPtr<nsIDOMEvent> domEvent;
  NS_NewDOMSpeechRecognitionError(getter_AddRefs(domEvent), nullptr, nullptr, nullptr);

  nsCOMPtr<nsIDOMSpeechRecognitionError> srError = do_QueryInterface(domEvent);
  srError->InitSpeechRecognitionError(NS_LITERAL_STRING("error"), true, false,
                                      aErrorCode, aMessage);
  nsRefPtr<SpeechEvent> event = new SpeechEvent(this, aErrorType);
  event->mError = srError;
  NS_DispatchToMainThread(event);
}

/*
 * Buffer audio samples into mAudioSamplesBuffer until aBufferSize.
 * Updates mBufferedSamples and returns the number of samples that were buffered.
 */
uint32_t
SpeechRecognition::FillSamplesBuffer(const int16_t* aSamples,
                                     uint32_t aSampleCount)
{
  MOZ_ASSERT(mBufferedSamples < mAudioSamplesPerChunk);
  MOZ_ASSERT(mAudioSamplesBuffer.get());

  int16_t* samplesBuffer = static_cast<int16_t*>(mAudioSamplesBuffer->Data());
  size_t samplesToCopy = std::min(aSampleCount,
                                  mAudioSamplesPerChunk - mBufferedSamples);

  memcpy(samplesBuffer + mBufferedSamples, aSamples,
         samplesToCopy * sizeof(int16_t));

  mBufferedSamples += samplesToCopy;
  return samplesToCopy;
}

/*
 * Split a samples buffer starting of a given size into
 * chunks of equal size. The chunks are stored in the array
 * received as argument.
 * Returns the offset of the end of the last chunk that was
 * created.
 */
uint32_t
SpeechRecognition::SplitSamplesBuffer(const int16_t* aSamplesBuffer,
                                      uint32_t aSampleCount,
                                      nsTArray<already_AddRefed<SharedBuffer> >& aResult)
{
  uint32_t chunkStart = 0;

  while (chunkStart + mAudioSamplesPerChunk <= aSampleCount) {
    nsRefPtr<SharedBuffer> chunk =
      SharedBuffer::Create(mAudioSamplesPerChunk * sizeof(int16_t));

    memcpy(chunk->Data(), aSamplesBuffer + chunkStart,
           mAudioSamplesPerChunk * sizeof(int16_t));

    aResult.AppendElement(chunk.forget());
    chunkStart += mAudioSamplesPerChunk;
  }

  return chunkStart;
}

AudioSegment*
SpeechRecognition::CreateAudioSegment(nsTArray<already_AddRefed<SharedBuffer> >& aChunks)
{
  AudioSegment* segment = new AudioSegment();
  for (uint32_t i = 0; i < aChunks.Length(); ++i) {
    const int16_t* chunkData =
      static_cast<const int16_t*>(aChunks[i].get()->Data());

    nsAutoTArray<const int16_t*, 1> channels;
    channels.AppendElement(chunkData);
    segment->AppendFrames(aChunks[i], channels, mAudioSamplesPerChunk);
  }

  return segment;
}

void
SpeechRecognition::FeedAudioData(already_AddRefed<SharedBuffer> aSamples,
                                 uint32_t aDuration,
                                 MediaStreamListener* aProvider)
{
  NS_ASSERTION(!NS_IsMainThread(),
               "FeedAudioData should not be called in the main thread");

  // Endpointer expects to receive samples in chunks whose size is a
  // multiple of its frame size.
  // Since we can't assume we will receive the frames in appropriate-sized
  // chunks, we must buffer and split them in chunks of mAudioSamplesPerChunk
  // (a multiple of Endpointer's frame size) before feeding to Endpointer.

  // ensure aSamples is deleted
  nsRefPtr<SharedBuffer> refSamples = aSamples;

  uint32_t samplesIndex = 0;
  const int16_t* samples = static_cast<int16_t*>(refSamples->Data());
  nsAutoTArray<already_AddRefed<SharedBuffer>, 5> chunksToSend;

  // fill up our buffer and make a chunk out of it, if possible
  if (mBufferedSamples > 0) {
    samplesIndex += FillSamplesBuffer(samples, aDuration);

    if (mBufferedSamples == mAudioSamplesPerChunk) {
      chunksToSend.AppendElement(mAudioSamplesBuffer.forget());
      mBufferedSamples = 0;
    }
  }

  // create sample chunks of correct size
  if (samplesIndex < aDuration) {
    samplesIndex += SplitSamplesBuffer(samples + samplesIndex,
                                       aDuration - samplesIndex,
                                       chunksToSend);
  }

  // buffer remaining samples
  if (samplesIndex < aDuration) {
    mBufferedSamples = 0;
    mAudioSamplesBuffer =
      SharedBuffer::Create(mAudioSamplesPerChunk * sizeof(int16_t));

    FillSamplesBuffer(samples + samplesIndex, aDuration - samplesIndex);
  }

  AudioSegment* segment = CreateAudioSegment(chunksToSend);
  nsRefPtr<SpeechEvent> event = new SpeechEvent(this, EVENT_AUDIO_DATA);
  event->mAudioSegment = segment;
  event->mProvider = aProvider;
  NS_DispatchToMainThread(event);

  return;
}

const char*
SpeechRecognition::GetName(FSMState aId)
{
  static const char* names[] = {
    "STATE_IDLE",
    "STATE_STARTING",
    "STATE_ESTIMATING",
    "STATE_WAITING_FOR_SPEECH",
    "STATE_RECOGNIZING",
    "STATE_WAITING_FOR_RESULT",
    "STATE_ABORTING"
  };

  MOZ_ASSERT(aId < STATE_COUNT);
  MOZ_ASSERT(ArrayLength(names) == STATE_COUNT);
  return names[aId];
}

const char*
SpeechRecognition::GetName(SpeechEvent* aEvent)
{
  static const char* names[] = {
    "EVENT_START",
    "EVENT_STOP",
    "EVENT_ABORT",
    "EVENT_AUDIO_DATA",
    "EVENT_AUDIO_ERROR",
    "EVENT_RECOGNITIONSERVICE_INTERMEDIATE_RESULT",
    "EVENT_RECOGNITIONSERVICE_FINAL_RESULT",
    "EVENT_RECOGNITIONSERVICE_ERROR"
  };

  MOZ_ASSERT(aEvent->mType < EVENT_COUNT);
  MOZ_ASSERT(ArrayLength(names) == EVENT_COUNT);
  return names[aEvent->mType];
}

NS_IMPL_ISUPPORTS1(SpeechRecognition::GetUserMediaStreamOptions, nsIMediaStreamOptions)

NS_IMETHODIMP
SpeechRecognition::GetUserMediaStreamOptions::GetFake(bool* aFake)
{
  *aFake = false;
  return NS_OK;
}

NS_IMETHODIMP
SpeechRecognition::GetUserMediaStreamOptions::GetAudio(bool* aAudio)
{
  *aAudio = true;
  return NS_OK;
}

NS_IMETHODIMP
SpeechRecognition::GetUserMediaStreamOptions::GetVideo(bool* aVideo)
{
  *aVideo = false;
  return NS_OK;
}

NS_IMETHODIMP
SpeechRecognition::GetUserMediaStreamOptions::GetPicture(bool* aPicture)
{
  *aPicture = false;
  return NS_OK;
}

NS_IMETHODIMP
SpeechRecognition::GetUserMediaStreamOptions::GetCamera(nsAString& aCamera)
{
  return NS_OK;
}

NS_IMETHODIMP
SpeechRecognition::GetUserMediaStreamOptions::GetAudioDevice(nsIMediaDevice** aAudioDevice)
{
  *aAudioDevice = nullptr;
  return NS_OK;
}

NS_IMETHODIMP
SpeechRecognition::GetUserMediaStreamOptions::GetVideoDevice(nsIMediaDevice** aVideoDevice)
{
  *aVideoDevice = nullptr;
  return NS_OK;
}

SpeechEvent::~SpeechEvent()
{
  delete mAudioSegment;
}

NS_IMETHODIMP
SpeechEvent::Run()
{
  mRecognition->ProcessEvent(this);
  return NS_OK;
}

NS_IMPL_ISUPPORTS1(SpeechRecognition::GetUserMediaSuccessCallback, nsIDOMGetUserMediaSuccessCallback)

NS_IMETHODIMP
SpeechRecognition::GetUserMediaSuccessCallback::OnSuccess(nsISupports* aStream)
{
  nsCOMPtr<nsIDOMLocalMediaStream> localStream = do_QueryInterface(aStream);
  mRecognition->StartRecording(static_cast<DOMLocalMediaStream*>(localStream.get()));
  return NS_OK;
}

NS_IMPL_ISUPPORTS1(SpeechRecognition::GetUserMediaErrorCallback, nsIDOMGetUserMediaErrorCallback)

NS_IMETHODIMP
SpeechRecognition::GetUserMediaErrorCallback::OnError(const nsAString& aError)
{
  int errorCode;

  if (aError.Equals(NS_LITERAL_STRING("PERMISSION_DENIED"))) {
    errorCode = nsIDOMSpeechRecognitionError::NOT_ALLOWED;
  } else {
    errorCode = nsIDOMSpeechRecognitionError::AUDIO_CAPTURE;
  }

  mRecognition->DispatchError(SpeechRecognition::EVENT_AUDIO_ERROR, errorCode,
                              aError);

  return NS_OK;
}

} // namespace dom
} // namespace mozilla
