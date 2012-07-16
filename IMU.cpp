/*
 * IMU.cpp
 *
 *  Created on: Jul 5, 2012
 *      Author: Dan Chianucci
 */

#include "IMU.h"
#include <math.h>

IMU::IMU() {
	beta=betaDef; // 2 * proportional gain
	//quaternion of sensor frame relative to auxiliary frame
	q0=1.0f;
	q1=0.0f;
	q2=0.0f;
	q3=0.0f;
}


void IMU::init()
{
	init(ADXL345_DEFAULT_ADDRESS,ITG3200_DEFAULT_ADDRESS,HMC5883L_DEFAULT_ADDRESS);
}

void IMU::init(uint8_t accAddr, uint8_t gyrAddr, uint8_t magAddr)
{
	acc=ADXL345(accAddr);
	gyr=ITG3200(gyrAddr);
	mag=HMC5883L(magAddr);

	acc.initialize();
	delay(100);
	gyr.initialize();
	delay(100);
	mag.initialize();
	delay(100);

	calibrateGyr();
	delay(100);
	calibrateMag(1);//bias of 1
	delay(100);
	calibrateAcc();

	delay(100);
	mag.setMode(HMC5883L_MODE_SINGLE);
	mag.setDataRate(HMC5883L_RATE_75); //75Hz data rate
}

void IMU::calibrateGyr()
{
	//Offsets are calculated in terms of gyro's coordinate system
	int16_t gyrSum[3]={};
	int16_t gyrVal[3]={};

	for (int i = 0; i < 200; i++)
	{
		gyr.getRotation(&gyrVal[0], &gyrVal[1], &gyrVal[2]);
		gyrSum[0] += gyrVal[0];
		gyrSum[1] += gyrVal[1];
		gyrSum[2] += gyrVal[2];
	}

	gyrOffsets[0] = gyrSum[0] / 200.0;
	gyrOffsets[1] = gyrSum[1] / 200.0;
	gyrOffsets[2] = gyrSum[2] / 200.0;
}

void IMU::calibrateMag(unsigned char gain)
{
	//Scales and Maxs calculated in terms of Magnetometer's coordinate system
	mag.setMeasurementBias(HMC5883L_BIAS_POSITIVE);//set positive bias
	mag.setGain(gain);

	int16_t x, y, z;
	int mx = 0, my = 0, mz = 0;
	mag.setMode(HMC5883L_MODE_SINGLE);

	for (int i = 0; i < 10; i++)
	{
		mag.getHeading(&x, &y, &z);
		if (x > mx)
			mx = x;
		if (y > my)
			my = y;
		if (z > mz)
			mz = z;
	}

	float max = 0;

	if (mx > max)
		max = mx;

	if (my > max)
		max = my;

	if (mz > max)
		max = mz;

	magMaxs[0] = mx;
	magMaxs[1] = my;
	magMaxs[2] = mz;

	magScales[0] = (float)mx/max; // calc scales
	magScales[1] = (float)my/max;
	magScales[2] = (float)mz/max;

	mag.setMeasurementBias(HMC5883L_BIAS_NORMAL);
}

void IMU::calibrateAcc()
{
	accOffsets[0] = 1;//0.00376390;
	accOffsets[1] = 1;//0.00376009;
	accOffsets[2] = 1;//0.00376265;
}

//Does not account for axis mis match or scaling or sign
void IMU::getValuesRaw(int16_t *accV, int16_t *gyrV, int16_t *magV)
{
	acc.getAcceleration(&accV[0],&accV[1],&accV[2]);

	gyr.getRotation(&gyrV[0],&gyrV[1],&gyrV[2]);

	mag.getHeading(&magV[0],&magV[1],&magV[2]);
}
//Accounts for Axis Mismatch, Sign, and Scaling
void IMU::getValuesScaled(float *accV, float *gyrV, float *magV)
{
	//TODO scale everything correctly
	getValuesRaw(accRaw,gyrRaw,magRaw);

	accV[0]=accRaw[AX]*accOffsets[AX]*AXS;
	accV[1]=accRaw[AY]*accOffsets[AY]*AYS;
	accV[2]=accRaw[AZ]*accOffsets[AZ]
	                              *AZS;

	gyrV[0]=(gyrRaw[GX]-gyrOffsets[GX])/14.375*PI/180*GXS;//* gains[0]
	gyrV[1]=(gyrRaw[GY]-gyrOffsets[GY])/14.375*PI/180*GXS;
	gyrV[2]=(gyrRaw[GZ]-gyrOffsets[GZ])/14.375*PI/180*GZS;

	magV[0]=magRaw[MX]*magScales[MX]*MXS;
	magV[1]=magRaw[MY]*magScales[MY]*MYS;
	magV[2]=magRaw[MZ]*magScales[MZ]*MZS;
}




void IMU::MadgwickAHRSupdate(float gx, float gy, float gz, float ax, float ay, float az, float mx, float my, float mz) {
//	float recipNorm;
//	float s0, s1, s2, s3;
//	float qDot1, qDot2, qDot3, qDot4;
//	float hx, hy;
//	float _2q0mx, _2q0my, _2q0mz, _2q1mx, _2bx, _2bz, _4bx, _4bz, _2q0, _2q1, _2q2, _2q3, _2q0q2, _2q2q3, q0q0, q0q1, q0q2, q0q3, q1q1, q1q2, q1q3, q2q2, q2q3, q3q3;
//
//	// Use IMU algorithm if magnetometer measurement invalid (avoids NaN in magnetometer normalisation)
//	if((mx == 0.0f) && (my == 0.0f) && (mz == 0.0f))
//	{
//		MadgwickAHRSupdateIMU(gx, gy, gz, ax, ay, az);
//		return;
//	}
//
//	// Rate of change of quaternion from gyroscope
//	qDot1 = 0.5f * (-q1 * gx - q2 * gy - q3 * gz);
//	qDot2 = 0.5f * (q0 * gx + q2 * gz - q3 * gy);
//	qDot3 = 0.5f * (q0 * gy - q1 * gz + q3 * gx);
//	qDot4 = 0.5f * (q0 * gz + q1 * gy - q2 * gx);
//
//	// Compute feedback only if accelerometer measurement valid (avoids NaN in accelerometer normalisation)
//	if(!((ax == 0.0f) && (ay == 0.0f) && (az == 0.0f))) {
//
//		// Normalise accelerometer measurement
//		recipNorm = invSqrt(ax * ax + ay * ay + az * az);
//		ax *= recipNorm;
//		ay *= recipNorm;
//		az *= recipNorm;
//
//		// Normalise magnetometer measurement
//		recipNorm = invSqrt(mx * mx + my * my + mz * mz);
//		mx *= recipNorm;
//		my *= recipNorm;
//		mz *= recipNorm;
//
//		// Auxiliary variables to avoid repeated arithmetic
//		_2q0mx = 2.0f * q0 * mx;
//		_2q0my = 2.0f * q0 * my;
//		_2q0mz = 2.0f * q0 * mz;
//		_2q1mx = 2.0f * q1 * mx;
//		_2q0 = 2.0f * q0;
//		_2q1 = 2.0f * q1;
//		_2q2 = 2.0f * q2;
//		_2q3 = 2.0f * q3;
//		_2q0q2 = 2.0f * q0 * q2;
//		_2q2q3 = 2.0f * q2 * q3;
//		q0q0 = q0 * q0;
//		q0q1 = q0 * q1;
//		q0q2 = q0 * q2;
//		q0q3 = q0 * q3;
//		q1q1 = q1 * q1;
//		q1q2 = q1 * q2;
//		q1q3 = q1 * q3;
//		q2q2 = q2 * q2;
//		q2q3 = q2 * q3;
//		q3q3 = q3 * q3;
//
//		// Reference direction of Earth's magnetic field
//		hx = mx * q0q0 - _2q0my * q3 + _2q0mz * q2 + mx * q1q1 + _2q1 * my * q2 + _2q1 * mz * q3 - mx * q2q2 - mx * q3q3;
//		hy = _2q0mx * q3 + my * q0q0 - _2q0mz * q1 + _2q1mx * q2 - my * q1q1 + my * q2q2 + _2q2 * mz * q3 - my * q3q3;
//		_2bx = sqrt(hx * hx + hy * hy);
//		_2bz = -_2q0mx * q2 + _2q0my * q1 + mz * q0q0 + _2q1mx * q3 - mz * q1q1 + _2q2 * my * q3 - mz * q2q2 + mz * q3q3;
//		_4bx = 2.0f * _2bx;
//		_4bz = 2.0f * _2bz;
//
//		// Gradient decent algorithm corrective step
//		s0 = -_2q2 * (2.0f * q1q3 - _2q0q2 - ax) + _2q1 * (2.0f * q0q1 + _2q2q3 - ay) - _2bz * q2 * (_2bx * (0.5f - q2q2 - q3q3) + _2bz * (q1q3 - q0q2) - mx) + (-_2bx * q3 + _2bz * q1) * (_2bx * (q1q2 - q0q3) + _2bz * (q0q1 + q2q3) - my) + _2bx * q2 * (_2bx * (q0q2 + q1q3) + _2bz * (0.5f - q1q1 - q2q2) - mz);
//		s1 = _2q3 * (2.0f * q1q3 - _2q0q2 - ax) + _2q0 * (2.0f * q0q1 + _2q2q3 - ay) - 4.0f * q1 * (1 - 2.0f * q1q1 - 2.0f * q2q2 - az) + _2bz * q3 * (_2bx * (0.5f - q2q2 - q3q3) + _2bz * (q1q3 - q0q2) - mx) + (_2bx * q2 + _2bz * q0) * (_2bx * (q1q2 - q0q3) + _2bz * (q0q1 + q2q3) - my) + (_2bx * q3 - _4bz * q1) * (_2bx * (q0q2 + q1q3) + _2bz * (0.5f - q1q1 - q2q2) - mz);
//		s2 = -_2q0 * (2.0f * q1q3 - _2q0q2 - ax) + _2q3 * (2.0f * q0q1 + _2q2q3 - ay) - 4.0f * q2 * (1 - 2.0f * q1q1 - 2.0f * q2q2 - az) + (-_4bx * q2 - _2bz * q0) * (_2bx * (0.5f - q2q2 - q3q3) + _2bz * (q1q3 - q0q2) - mx) + (_2bx * q1 + _2bz * q3) * (_2bx * (q1q2 - q0q3) + _2bz * (q0q1 + q2q3) - my) + (_2bx * q0 - _4bz * q2) * (_2bx * (q0q2 + q1q3) + _2bz * (0.5f - q1q1 - q2q2) - mz);
//		s3 = _2q1 * (2.0f * q1q3 - _2q0q2 - ax) + _2q2 * (2.0f * q0q1 + _2q2q3 - ay) + (-_4bx * q3 + _2bz * q1) * (_2bx * (0.5f - q2q2 - q3q3) + _2bz * (q1q3 - q0q2) - mx) + (-_2bx * q0 + _2bz * q2) * (_2bx * (q1q2 - q0q3) + _2bz * (q0q1 + q2q3) - my) + _2bx * q1 * (_2bx * (q0q2 + q1q3) + _2bz * (0.5f - q1q1 - q2q2) - mz);
//		recipNorm = invSqrt(s0 * s0 + s1 * s1 + s2 * s2 + s3 * s3); // normalise step magnitude
//		s0 *= recipNorm;
//		s1 *= recipNorm;
//		s2 *= recipNorm;
//		s3 *= recipNorm;
//
//		// Apply feedback step
//		qDot1 -= beta * s0;
//		qDot2 -= beta * s1;
//		qDot3 -= beta * s2;
//		qDot4 -= beta * s3;
//	}
//
//	// Integrate rate of change of quaternion to yield quaternion
//	q0 += qDot1 * (1.0f / sampleFreq);
//	q1 += qDot2 * (1.0f / sampleFreq);
//	q2 += qDot3 * (1.0f / sampleFreq);
//	q3 += qDot4 * (1.0f / sampleFreq);
//
//	// Normalise quaternion
//	recipNorm = invSqrt(q0 * q0 + q1 * q1 + q2 * q2 + q3 * q3);
//	q0 *= recipNorm;
//	q1 *= recipNorm;
//	q2 *= recipNorm;
//	q3 *= recipNorm;

	  float norm;
	  float hx, hy, hz, bx, bz;
	  float vx, vy, vz, wx, wy, wz;
	  float ex, ey, ez;

	  // auxiliary variables to reduce number of repeated operations
	  float q0q0 = q0*q0;
	  float q0q1 = q0*q1;
	  float q0q2 = q0*q2;
	  float q0q3 = q0*q3;
	  float q1q1 = q1*q1;
	  float q1q2 = q1*q2;
	  float q1q3 = q1*q3;
	  float q2q2 = q2*q2;
	  float q2q3 = q2*q3;
	  float q3q3 = q3*q3;

	  // normalise the measurements
	  norm = sqrt(ax*ax + ay*ay + az*az);
	  ax = ax / norm;
	  ay = ay / norm;
	  az = az / norm;
	  norm = sqrt(mx*mx + my*my + mz*mz);
	  mx = mx / norm;
	  my = my / norm;
	  mz = mz / norm;

	  // compute reference direction of flux
	  hx = 2*mx*(0.5 - q2q2 - q3q3) + 2*my*(q1q2 - q0q3) + 2*mz*(q1q3 + q0q2);
	  hy = 2*mx*(q1q2 + q0q3) + 2*my*(0.5 - q1q1 - q3q3) + 2*mz*(q2q3 - q0q1);
	  hz = 2*mx*(q1q3 - q0q2) + 2*my*(q2q3 + q0q1) + 2*mz*(0.5 - q1q1 - q2q2);
	  bx = sqrt((hx*hx) + (hy*hy));
	  bz = hz;

	  // estimated direction of gravity and flux (v and w)
	  vx = 2*(q1q3 - q0q2);
	  vy = 2*(q0q1 + q2q3);
	  vz = q0q0 - q1q1 - q2q2 + q3q3;
	  wx = 2*bx*(0.5 - q2q2 - q3q3) + 2*bz*(q1q3 - q0q2);
	  wy = 2*bx*(q1q2 - q0q3) + 2*bz*(q0q1 + q2q3);
	  wz = 2*bx*(q0q2 + q1q3) + 2*bz*(0.5 - q1q1 - q2q2);

	  // error is sum of cross product between reference direction of fields and direction measured by sensors
	  ex = (ay*vz - az*vy) + (my*wz - mz*wy);
	  ey = (az*vx - ax*vz) + (mz*wx - mx*wz);
	  ez = (ax*vy - ay*vx) + (mx*wy - my*wx);

	  // integral error scaled integral gain
	  exInt = exInt + ex*Ki;
	  eyInt = eyInt + ey*Ki;
	  ezInt = ezInt + ez*Ki;

	  // adjusted gyroscope measurements
	  gx = gx + Kp*ex + exInt;
	  gy = gy + Kp*ey + eyInt;
	  gz = gz + Kp*ez + ezInt;

	  // integrate quaternion rate and normalise
	  q0 = q0 + (-q1*gx - q2*gy - q3*gz)*halfT;
	  q1 = q1 + (q0*gx + q2*gz - q3*gy)*halfT;
	  q2 = q2 + (q0*gy - q1*gz + q3*gx)*halfT;
	  q3 = q3 + (q0*gz + q1*gy - q2*gx)*halfT;

	  // normalise quaternion
	  norm = sqrt(q0*q0 + q1*q1 + q2*q2 + q3*q3);
	  q0 = q0 / norm;
	  q1 = q1 / norm;
	  q2 = q2 / norm;
	  q3 = q3 / norm;
}

void IMU::MadgwickAHRSupdateIMU(float gx, float gy, float gz, float ax, float ay, float az) {
	float recipNorm;
	float s0, s1, s2, s3;
	float qDot1, qDot2, qDot3, qDot4;
	float _2q0, _2q1, _2q2, _2q3, _4q0, _4q1, _4q2 ,_8q1, _8q2, q0q0, q1q1, q2q2, q3q3;

	// Rate of change of quaternion from gyroscope
	qDot1 = 0.5f * (-q1 * gx - q2 * gy - q3 * gz);
	qDot2 = 0.5f * (q0 * gx + q2 * gz - q3 * gy);
	qDot3 = 0.5f * (q0 * gy - q1 * gz + q3 * gx);
	qDot4 = 0.5f * (q0 * gz + q1 * gy - q2 * gx);

	// Compute feedback only if accelerometer measurement valid (avoids NaN in accelerometer normalisation)
	if(!((ax == 0.0f) && (ay == 0.0f) && (az == 0.0f))) {

		// Normalise accelerometer measurement
		recipNorm = invSqrt(ax * ax + ay * ay + az * az);
		ax *= recipNorm;
		ay *= recipNorm;
		az *= recipNorm;

		// Auxiliary variables to avoid repeated arithmetic
		_2q0 = 2.0f * q0;
		_2q1 = 2.0f * q1;
		_2q2 = 2.0f * q2;
		_2q3 = 2.0f * q3;
		_4q0 = 4.0f * q0;
		_4q1 = 4.0f * q1;
		_4q2 = 4.0f * q2;
		_8q1 = 8.0f * q1;
		_8q2 = 8.0f * q2;
		q0q0 = q0 * q0;
		q1q1 = q1 * q1;
		q2q2 = q2 * q2;
		q3q3 = q3 * q3;

		// Gradient decent algorithm corrective step
		s0 = _4q0 * q2q2 + _2q2 * ax + _4q0 * q1q1 - _2q1 * ay;
		s1 = _4q1 * q3q3 - _2q3 * ax + 4.0f * q0q0 * q1 - _2q0 * ay - _4q1 + _8q1 * q1q1 + _8q1 * q2q2 + _4q1 * az;
		s2 = 4.0f * q0q0 * q2 + _2q0 * ax + _4q2 * q3q3 - _2q3 * ay - _4q2 + _8q2 * q1q1 + _8q2 * q2q2 + _4q2 * az;
		s3 = 4.0f * q1q1 * q3 - _2q1 * ax + 4.0f * q2q2 * q3 - _2q2 * ay;
		recipNorm = invSqrt(s0 * s0 + s1 * s1 + s2 * s2 + s3 * s3); // normalise step magnitude
		s0 *= recipNorm;
		s1 *= recipNorm;
		s2 *= recipNorm;
		s3 *= recipNorm;

		// Apply feedback step
		qDot1 -= beta * s0;
		qDot2 -= beta * s1;
		qDot3 -= beta * s2;
		qDot4 -= beta * s3;
	}

	// Integrate rate of change of quaternion to yield quaternion
	q0 += qDot1 * (1.0f / sampleFreq);
	q1 += qDot2 * (1.0f / sampleFreq);
	q2 += qDot3 * (1.0f / sampleFreq);
	q3 += qDot4 * (1.0f / sampleFreq);

	// Normalise quaternion
	recipNorm = invSqrt(q0 * q0 + q1 * q1 + q2 * q2 + q3 * q3);
	q0 *= recipNorm;
	q1 *= recipNorm;
	q2 *= recipNorm;
	q3 *= recipNorm;
}

void IMU::Update()
{
	getValuesScaled(accScaled,gyrScaled,magScaled);

	MadgwickAHRSupdate(gyrScaled[0],gyrScaled[1],gyrScaled[2],
						accScaled[0],accScaled[1],accScaled[2],
						magScaled[0],magScaled[1],magScaled[2]);
}

void IMU::getQuaternion(float *quatArr)
{
	quatArr[0]=q0;
	quatArr[1]=q1;
	quatArr[2]=q2;
	quatArr[3]=q3;
}

void IMU::getData(float *q, float *a, float *g, float *m )
{
	q[0]=q0;
	q[1]=q1;
	q[2]=q2;
	q[3]=q3;

	a[0]=accScaled[0];
	a[1]=accScaled[1];
	a[2]=accScaled[2];

	g[0]=gyrScaled[0];
	g[1]=gyrScaled[1];
	g[2]=gyrScaled[2];

	m[0]=magScaled[0];
	m[1]=magScaled[1];
	m[2]=magScaled[2];

}

float IMU::invSqrt(float x) {
	float halfx = 0.5f * x;
	float y = x;
	long i = *(long*)&y;
	i = 0x5f3759df - (i>>1);
	y = *(float*)&i;
	y = y * (1.5f - (halfx * y * y));
	return y;
}
