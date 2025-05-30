#pragma once
#ifndef _MAYBE_
#define _MAYBE_

template <typename T> struct Maybe {
    T data;
    bool none;
    Maybe() {
        /* data is default initialized*/
        none = true;
    }

    Maybe(const T& data) {
        this->data = data;
        none = false;
    }

    const T& just() const {
        return data;
    }

    bool isnone() const {
        return none;
    }
};


#endif