/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "lvgl.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace reactive {

class Subject {
public:
    Subject() = default;
    virtual ~Subject();

    Subject(const Subject&) = delete;
    Subject& operator=(const Subject&) = delete;

    lv_subject_t* native();
    const lv_subject_t* native() const;
    lv_subject_type_t type() const;

    void notify();

protected:
    lv_subject_t subject_{};
    bool initialized_ = false;

    void deinit();
};

class IntSubject final : public Subject {
public:
    explicit IntSubject(int32_t value = 0);

    int32_t value() const;
    int32_t previous() const;
    void set(int32_t value);
    void set_min(int32_t value);
    void set_max(int32_t value);
};

class BoolSubject final : public Subject {
public:
    explicit BoolSubject(bool value = false);

    bool value() const;
    bool previous() const;
    void set(bool value);
    void toggle();
};

#if LV_USE_FLOAT
class FloatSubject final : public Subject {
public:
    explicit FloatSubject(float value = 0.0F);

    float value() const;
    float previous() const;
    void set(float value);
    void set_min(float value);
    void set_max(float value);
};
#endif

template <std::size_t Size>
class StringSubject final : public Subject {
public:
    explicit StringSubject(const char* value = "") {
        lv_subject_init_string(&subject_, buffer_.data(), previous_buffer_.data(), Size, value);
        initialized_ = true;
    }

    const char* value() const {
        return lv_subject_get_string(native_mutable());
    }

    const char* previous() const {
        return lv_subject_get_previous_string(native_mutable());
    }

    void set(const char* value) {
        lv_subject_copy_string(native(), value ? value : "");
    }

private:
    lv_subject_t* native_mutable() const {
        return const_cast<lv_subject_t*>(&subject_);
    }

    static_assert(Size > 0, "StringSubject requires storage for the null terminator");

    std::array<char, Size> buffer_{};
    std::array<char, Size> previous_buffer_{};
};

template <typename T = void>
class PointerSubject final : public Subject {
public:
    explicit PointerSubject(T* value = nullptr) {
        lv_subject_init_pointer(&subject_, value);
        initialized_ = true;
    }

    T* value() const {
        return static_cast<T*>(const_cast<void*>(lv_subject_get_pointer(native_mutable())));
    }

    T* previous() const {
        return static_cast<T*>(const_cast<void*>(lv_subject_get_previous_pointer(native_mutable())));
    }

    void set(T* value) {
        lv_subject_set_pointer(native(), value);
    }

private:
    lv_subject_t* native_mutable() const {
        return const_cast<lv_subject_t*>(&subject_);
    }
};

class ColorSubject final : public Subject {
public:
    explicit ColorSubject(lv_color_t value);

    lv_color_t value() const;
    lv_color_t previous() const;
    void set(lv_color_t value);
};

template <std::size_t Size>
class GroupSubject final : public Subject {
public:
    explicit GroupSubject(const std::array<Subject*, Size>& subjects) {
        for(std::size_t i = 0; i < Size; ++i) {
            subjects_[i] = subjects[i] ? subjects[i]->native() : nullptr;
        }
        lv_subject_init_group(&subject_, subjects_.data(), Size);
        initialized_ = true;
    }

    lv_subject_t* element(std::size_t index) {
        return lv_subject_get_group_element(native(), static_cast<int32_t>(index));
    }

private:
    static_assert(Size > 0, "GroupSubject requires at least one child subject");

    std::array<lv_subject_t*, Size> subjects_{};
};

} // namespace reactive
