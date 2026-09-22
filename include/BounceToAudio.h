#ifndef LMMS_BOUNCE_TO_AUDIO_H
#define LMMS_BOUNCE_TO_AUDIO_H

namespace lmms
{
class MidiClip;
namespace gui
{
class TrackView;
// A null clip bounces the complete track in the Song Editor.
void bounceToAudio(TrackView* sourceView, MidiClip* clip = nullptr);
}
}

#endif
