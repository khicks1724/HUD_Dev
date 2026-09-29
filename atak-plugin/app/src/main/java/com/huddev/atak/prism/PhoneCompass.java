package com.huddev.atak.prism;

import android.content.Context;
import android.hardware.GeomagneticField;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;

/**
 * True heading of the phone's back camera (phone held upright, camera
 * pointing the way you look). Used to give the HUD an absolute heading
 * until it has its own magnetometer.
 */
final class PhoneCompass implements SensorEventListener {
    private final SensorManager sm;
    private final float[] rot = new float[9], remap = new float[9], ori = new float[3];
    private volatile float magneticAzimuth = Float.NaN;

    PhoneCompass(Context c) {
        sm = (SensorManager) c.getSystemService(Context.SENSOR_SERVICE);
    }

    void start() {
        if (sm == null) return;
        Sensor s = sm.getDefaultSensor(Sensor.TYPE_ROTATION_VECTOR);
        if (s != null) sm.registerListener(this, s, SensorManager.SENSOR_DELAY_UI);
    }

    void stop() {
        if (sm != null) sm.unregisterListener(this);
    }

    @Override
    public void onSensorChanged(SensorEvent e) {
        SensorManager.getRotationMatrixFromVector(rot, e.values);
        // camera (-Z of the screen) as the pointing axis when held upright
        SensorManager.remapCoordinateSystem(rot, SensorManager.AXIS_X, SensorManager.AXIS_Z, remap);
        SensorManager.getOrientation(remap, ori);
        magneticAzimuth = (float) Math.toDegrees(ori[0]);
    }

    @Override
    public void onAccuracyChanged(Sensor sensor, int accuracy) {
    }

    /** True heading in degrees, or NaN if the sensor has not reported yet. */
    float trueHeading(double lat, double lon, double altM) {
        float m = magneticAzimuth;
        if (Float.isNaN(m)) return Float.NaN;
        float decl = new GeomagneticField((float) lat, (float) lon, (float) altM, System.currentTimeMillis()).getDeclination();
        float t = (m + decl) % 360f;
        return t < 0 ? t + 360f : t;
    }
}
