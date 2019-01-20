/* Copyright (C) 2014 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package android.media.session;

import android.app.PendingIntent;
import android.media.AudioAttributes;
import android.media.MediaMetadata;
import android.media.session.ControllerLink;
import android.media.session.PlaybackState;
import android.media.session.MediaSession;
import android.os.Bundle;
import android.os.ResultReceiver;

/**
 * Interface to a MediaSession in the system.
 * @hide
 */
interface ISession {
    void sendEvent(String event, in Bundle data);
    ControllerLink getController();
    void setFlags(int flags);
    void setActive(boolean active);
    void setMediaButtonReceiver(in PendingIntent mbr);
    void setLaunchPendingIntent(in PendingIntent pi);
    void destroySession();

    // These commands are for the TransportPerformer
    void setMetadata(in MediaMetadata metadata, long duration, String metadataDescription);
    void setPlaybackState(in PlaybackState state);
    // TODO(b/122432476): Replace List with MediaParceledListSlice
    void setQueue(in List<MediaSession.QueueItem> queue);
    void setQueueTitle(CharSequence title);
    void setExtras(in Bundle extras);
    void setRatingType(int type);

    // These commands relate to volume handling
    void setPlaybackToLocal(in AudioAttributes attributes);
    void setPlaybackToRemote(int control, int max);
    void setCurrentVolume(int currentVolume);
}
