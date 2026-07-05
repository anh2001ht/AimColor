#pragma once

struct Vector2 {
 float x;
 float y;

 Vector2(float _x, float _y) : x(_x), y(_y) {}

 Vector2 operator+(const Vector2& other) const {
 return Vector2(x + other.x, y + other.y);
 }

 Vector2 operator-(const Vector2& other) const {
 return Vector2(x - other.x, y - other.y);
 }

 Vector2 operator*(float scalar) const {
 return Vector2(x * scalar, y * scalar);
 }

 Vector2 operator/(float scalar) const {
 return Vector2(x / scalar, y / scalar);
 }

 bool operator==(const Vector2& other) const {
 return x == other.x && y == other.y;
 }

 bool operator!=(const Vector2& other) const {
 return !(*this == other);
 }
};
