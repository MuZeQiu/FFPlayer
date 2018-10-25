#include "ff_blocking_audio_player.h"


namespace FFPlayer {
std::mutex ff_blocking_audio_player::m_;
ff_blocking_audio_player *ff_blocking_audio_player::audio_player_ = NULL;
}
