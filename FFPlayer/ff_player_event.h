#ifndef FF_PLAYER_EVENT_H
#define FF_PLAYER_EVENT_H


namespace FFPlayer {
class ff_player_event {
public:
    virtual void player_pause() {}
    virtual void player_start() {}
    virtual void player_next(const char*) {}
    virtual void player_last(const char*) {}
    virtual void player_slide(int) {}
    virtual void slider_release() {}
    virtual void player_resize() {}
    virtual void player_close() {}
};
}

#endif // FF_PLAYER_EVENT_H
