#include "Vec3D.h"
#include <iomanip>

Vec3f::Vec3f(float xv, float yv, float zv) : x(xv), y(yv), z(zv) {}

std::ostream& operator<< (std::ostream& stream, const Vec3f& vec) {
    int width = stream.width();

    stream << std::setw(0) << "{" << std::setw(width) << vec.x << ", " << std::setw(width) << vec.y << ", " << std::setw(width) << vec.z << "}";

    return stream;
}

Vec3s::Vec3s(int16_t xv, int16_t yv, int16_t zv) : x(xv), y(yv), z(zv) {}

std::ostream& operator<< (std::ostream& stream, const Vec3s& vec) {
    int width = stream.width();

    stream << std::setw(0) << "{" << std::setw(width) << vec.x << ", " << std::setw(width) << vec.y << ", " << std::setw(width) << vec.z << "}";
    return stream;
}

Vec3i::Vec3i(int32_t xv, int32_t yv, int32_t zv) : x(xv), y(yv), z(zv) {}

std::ostream& operator<< (std::ostream& stream, const Vec3i& vec) {
    int width = stream.width();

    stream << std::setw(0) << "{" << std::setw(width) << vec.x << ", " << std::setw(width) << vec.y << ", " << std::setw(width) << vec.z << "}";
    return stream;
}

Vec2f::Vec2f(float xv, float zv) : x(xv), z(zv) {}

std::ostream& operator<< (std::ostream& stream, const Vec2f& vec) {
    int width = stream.width();

    stream << std::setw(0) << "{" << std::setw(width) << vec.x << ", " << std::setw(width) << vec.z << "}";
    return stream;
}

Vec4f::Vec4f(float xv, float yv, float zv, float wv) : x(xv), y(yv), z(zv), w(wv) {}

std::ostream& operator<< (std::ostream& stream, const Vec4f& vec) {
    int width = stream.width();

    stream << std::setw(0) << "{" << std::setw(width) << vec.x << ", " << std::setw(width) << vec.y << ", " << std::setw(width) << vec.z << ", " << std::setw(width) << vec.w << "}";
    return stream;
}

Vec4s::Vec4s(int16_t xv, int16_t yv, int16_t zv, int16_t wv) : x(xv), y(yv), z(zv), w(wv) {}

std::ostream& operator<< (std::ostream& stream, const Vec4s& vec) {
    int width = stream.width();

    stream << std::setw(0) << "{" << std::setw(width) << vec.x << ", " << std::setw(width) << vec.y << ", " << std::setw(width) << vec.z << ", " << std::setw(width) << vec.w << "}";
    return stream;
}
