#include "Math.h"


void GB::Utils::Print()
{
	std::cout << "Welcome to GB Engine" << std::endl;
}

void GB::Utils::SecondTest()
{
	std::cout << "Welcome to the second test" << std::endl;
}

void GB::Core::Example_Command::execute()
{
	std::cout << "Command example 1" << std::endl;
}



DLL Vector3 Vector3::Up()
{
	return  Vector3(0, 1, 0);
}

DLL Vector3 Vector3::Down()
{
	return  Vector3(0, -1, 0);

}

DLL Vector3 Vector3::Left()
{
	return Vector3(-1, 0, 0);
}

DLL Vector3 Vector3::Right()
{
	return Vector3(1, 0, 0);

}

DLL Vector3 Vector3::One()
{
	return Vector3(-1, 0, 0);

}

Vector3::Vector3()
{
	x = 0;
	y = 0;
	z = 0;
}

Vector3::Vector3(float _x, float _y, float _z)
{
	x = _x;
	y = _y;
	z = _z;

}

Vector3 & Vector3::operator+(const Vector3& v)
{
	// TODO: insertar una instrucción return aquí

	Vector3 vector;

	vector.x = this->x + v.x;
	vector.y = this->y + v.y;
	vector.z = this->z + v.z;

	return vector;
}

DLL Vector3 & Vector3::operator-(const Vector3 & v)
{
	Vector3 vector;

	vector.x = this->x - v.x;
	vector.y = this->y - v.y;
	vector.z = this->z - v.z;

	return vector;
}

DLL Vector3 & Vector3::operator+=(const Vector3 & v)
{
	this->x += v.x;
	this->y += v.y;
	this->z += v.z;

	return *this;
}

DLL Vector3 & Vector3::operator-=(const Vector3 & v)
{
	this->x -= v.x;
	this->y -= v.y;
	this->z -= v.z;

	return *this;
}

DLL Vector3 & Vector3::operator*(const float & x)
{
	Vector3 vector;

	vector.x = this->x *x;
	vector.y = this->y *x;
	vector.z = this->z *x;

	return vector;
}

DLL Vector3 & Vector3::operator*=(const float & x)
{

	this->x *= x;
	this->y *= x;
	this->z *= x;

	return *this;
}

DLL Vector3 & Vector3::operator/(const float & x)
{
	Vector3 vector;

	vector.x = this->x /x;
	vector.y = this->y /x;
	vector.z = this->z /x;

	return vector;
}

DLL Vector3 & Vector3::operator/=(const float & x)
{
	Vector3 vector;

	vector.x = this->x /x;
	vector.y = this->y /x;
	vector.z = this->z /x;

	return vector;
}

DLL bool & Vector3::operator==(const Vector3 & v)
{
	bool r = x == v.x && y == v.y && z == v.z;

	return r;
}

DLL float Vector3::Magnitude()
{
	return std::sqrt(x*x + y * y + z * z);
}

DLL Vector3 Vector3::Normalized()
{
	float m = Magnitude();

	x /= m;
	y /= m;
	z /= m;

	return *this;
}
