#ifndef LMMS_BOUNCE_TO_AUDIO_H
#define LMMS_BOUNCE_TO_AUDIO_H

namespace lmms
{
class MidiClip;
class SampleTrack;
namespace gui
{
class TrackView;
//! Renders the track (or just @p clip) to a sample track placed below it, routed to the same mixer channel,
//! and mutes the source so the bounce replaces it. A null clip bounces the complete track in the Song Editor.
//! Returns the new track, or nullptr if nothing was bounced.
SampleTrack* bounceToAudio(TrackView* sourceView, MidiClip* clip = nullptr);

//! Bounce that remembers its source, so unfreezeTrack() can bring the editable track back
void freezeTrack(TrackView* sourceView);
//! For a frozen track: unmute its source, delete the frozen audio and remove the frozen track
void unfreezeTrack(TrackView* frozenView);
} // namespace gui
} // namespace lmms

#endif // LMMS_BOUNCE_TO_AUDIO_H
