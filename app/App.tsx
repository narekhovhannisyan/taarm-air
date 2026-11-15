import { StatusBar } from 'expo-status-bar';
import { useCallback, useEffect, useRef, useState } from 'react';
import {
  ActivityIndicator,
  PermissionsAndroid,
  Platform,
  Pressable,
  SafeAreaView,
  StyleSheet,
  Text,
  View,
} from 'react-native';
import { BleError, BleManager, Characteristic, Device, State } from 'react-native-ble-plx';
import {
  DEVICE_NAME,
  UUIDS,
  decodeFloatLE,
  decodeUint16LE,
  decodeUint8,
  encodeFloatLE,
  encodeUint8,
} from './src/ble';

const manager = new BleManager();

type ConnState = 'idle' | 'scanning' | 'connecting' | 'connected' | 'error';

async function requestBlePermissions(): Promise<boolean> {
  if (Platform.OS !== 'android') {
    return true;
  }
  if (Platform.Version >= 31) {
    const result = await PermissionsAndroid.requestMultiple([
      PermissionsAndroid.PERMISSIONS.BLUETOOTH_SCAN,
      PermissionsAndroid.PERMISSIONS.BLUETOOTH_CONNECT,
      PermissionsAndroid.PERMISSIONS.ACCESS_FINE_LOCATION,
    ]);
    return Object.values(result).every((v) => v === PermissionsAndroid.RESULTS.GRANTED);
  }
  const result = await PermissionsAndroid.request(
    PermissionsAndroid.PERMISSIONS.ACCESS_FINE_LOCATION,
  );
  return result === PermissionsAndroid.RESULTS.GRANTED;
}

export default function App() {
  const [conn, setConn] = useState<ConnState>('idle');
  const [error, setError] = useState<string | null>(null);
  const [temperature, setTemperature] = useState<number | null>(null);
  const [setpoint, setSetpoint] = useState(28);
  const [mode, setMode] = useState<0 | 1>(0);
  const [fanDuty, setFanDuty] = useState(50);
  const [rpm, setRpm] = useState(0);
  const deviceRef = useRef<Device | null>(null);
  const subsRef = useRef<{ remove: () => void }[]>([]);

  const cleanup = useCallback(async () => {
    subsRef.current.forEach((s) => s.remove());
    subsRef.current = [];
    manager.stopDeviceScan();
    if (deviceRef.current) {
      try {
        await deviceRef.current.cancelConnection();
      } catch {
        // ignore
      }
      deviceRef.current = null;
    }
  }, []);

  useEffect(() => {
    return () => {
      void cleanup();
      manager.destroy();
    };
  }, [cleanup]);

  const onTempNotify = (error: BleError | null, c: Characteristic | null) => {
    if (error || !c) return;
    const v = decodeFloatLE(c.value);
    if (v !== null) setTemperature(v);
  };

  const onDutyNotify = (error: BleError | null, c: Characteristic | null) => {
    if (error || !c) return;
    const v = decodeUint8(c.value);
    if (v !== null) setFanDuty(v);
  };

  const onRpmNotify = (error: BleError | null, c: Characteristic | null) => {
    if (error || !c) return;
    const v = decodeUint16LE(c.value);
    if (v !== null) setRpm(v);
  };

  const connectToDevice = async (device: Device) => {
    setConn('connecting');
    const connected = await device.connect();
    deviceRef.current = connected;
    await connected.discoverAllServicesAndCharacteristics();

    const tempC = await connected.readCharacteristicForService(UUIDS.service, UUIDS.temperature);
    const setC = await connected.readCharacteristicForService(UUIDS.service, UUIDS.setpoint);
    const modeC = await connected.readCharacteristicForService(UUIDS.service, UUIDS.mode);
    const dutyC = await connected.readCharacteristicForService(UUIDS.service, UUIDS.fanDuty);
    const rpmC = await connected.readCharacteristicForService(UUIDS.service, UUIDS.rpm);

    const t = decodeFloatLE(tempC.value);
    const s = decodeFloatLE(setC.value);
    const m = decodeUint8(modeC.value);
    const d = decodeUint8(dutyC.value);
    const r = decodeUint16LE(rpmC.value);
    if (t !== null) setTemperature(t);
    if (s !== null) setSetpoint(Math.round(s * 10) / 10);
    if (m === 0 || m === 1) setMode(m);
    if (d !== null) setFanDuty(d);
    if (r !== null) setRpm(r);

    const s1 = connected.monitorCharacteristicForService(
      UUIDS.service,
      UUIDS.temperature,
      onTempNotify,
    );
    const s2 = connected.monitorCharacteristicForService(UUIDS.service, UUIDS.fanDuty, onDutyNotify);
    const s3 = connected.monitorCharacteristicForService(UUIDS.service, UUIDS.rpm, onRpmNotify);
    subsRef.current = [s1, s2, s3];

    connected.onDisconnected(() => {
      setConn('idle');
      deviceRef.current = null;
    });

    setConn('connected');
  };

  const scanAndConnect = async () => {
    setError(null);
    const ok = await requestBlePermissions();
    if (!ok) {
      setError('Bluetooth permission denied');
      setConn('error');
      return;
    }

    const state = await manager.state();
    if (state !== State.PoweredOn) {
      setError(`Bluetooth is ${state}. Turn it on and try again.`);
      setConn('error');
      return;
    }

    await cleanup();
    setConn('scanning');

    const timeout = setTimeout(() => {
      manager.stopDeviceScan();
      setError(`No device named ${DEVICE_NAME} found`);
      setConn('error');
    }, 15000);

    manager.startDeviceScan(null, { allowDuplicates: false }, (err, device) => {
      if (err) {
        clearTimeout(timeout);
        manager.stopDeviceScan();
        setError(err.message);
        setConn('error');
        return;
      }
      if (!device) return;
      const name = device.name ?? device.localName ?? '';
      if (name !== DEVICE_NAME) return;

      clearTimeout(timeout);
      manager.stopDeviceScan();
      void connectToDevice(device).catch((e: Error) => {
        setError(e.message);
        setConn('error');
      });
    });
  };

  const writeSetpoint = async (next: number) => {
    const clamped = Math.min(40, Math.max(15, Math.round(next * 10) / 10));
    setSetpoint(clamped);
    const device = deviceRef.current;
    if (!device) return;
    await device.writeCharacteristicWithResponseForService(
      UUIDS.service,
      UUIDS.setpoint,
      encodeFloatLE(clamped),
    );
  };

  const writeMode = async (next: 0 | 1) => {
    setMode(next);
    const device = deviceRef.current;
    if (!device) return;
    await device.writeCharacteristicWithResponseForService(
      UUIDS.service,
      UUIDS.mode,
      encodeUint8(next),
    );
  };

  const writeDuty = async (next: number) => {
    const clamped = Math.min(100, Math.max(0, Math.round(next)));
    setFanDuty(clamped);
    const device = deviceRef.current;
    if (!device || mode !== 1) return;
    await device.writeCharacteristicWithResponseForService(
      UUIDS.service,
      UUIDS.fanDuty,
      encodeUint8(clamped),
    );
  };

  const busy = conn === 'scanning' || conn === 'connecting';

  return (
    <SafeAreaView style={styles.safe}>
      <StatusBar style="dark" />
      <View style={styles.header}>
        <Text style={styles.brand}>taarm</Text>
        <Text style={styles.subtitle}>Thermostat</Text>
      </View>

      <View style={styles.card}>
        <Text style={styles.label}>Measured</Text>
        <Text style={styles.tempBig}>
          {temperature === null ? '—' : `${temperature.toFixed(1)}°C`}
        </Text>
        <Text style={styles.meta}>
          Fan {fanDuty}% · {rpm} RPM · {mode === 0 ? 'Auto' : 'Manual'}
        </Text>
      </View>

      <View style={styles.card}>
        <Text style={styles.label}>Target temperature</Text>
        <View style={styles.row}>
          <Pressable
            style={styles.stepBtn}
            disabled={conn !== 'connected'}
            onPress={() => void writeSetpoint(setpoint - 0.5)}
          >
            <Text style={styles.stepText}>−</Text>
          </Pressable>
          <Text style={styles.setpoint}>{setpoint.toFixed(1)}°C</Text>
          <Pressable
            style={styles.stepBtn}
            disabled={conn !== 'connected'}
            onPress={() => void writeSetpoint(setpoint + 0.5)}
          >
            <Text style={styles.stepText}>+</Text>
          </Pressable>
        </View>
      </View>

      <View style={styles.card}>
        <Text style={styles.label}>Mode</Text>
        <View style={styles.row}>
          <Pressable
            style={[styles.modeBtn, mode === 0 && styles.modeBtnActive]}
            disabled={conn !== 'connected'}
            onPress={() => void writeMode(0)}
          >
            <Text style={[styles.modeText, mode === 0 && styles.modeTextActive]}>Auto</Text>
          </Pressable>
          <Pressable
            style={[styles.modeBtn, mode === 1 && styles.modeBtnActive]}
            disabled={conn !== 'connected'}
            onPress={() => void writeMode(1)}
          >
            <Text style={[styles.modeText, mode === 1 && styles.modeTextActive]}>Manual</Text>
          </Pressable>
        </View>

        {mode === 1 && (
          <>
            <Text style={[styles.label, { marginTop: 16 }]}>Fan duty</Text>
            <View style={styles.row}>
              <Pressable
                style={styles.stepBtn}
                disabled={conn !== 'connected'}
                onPress={() => void writeDuty(fanDuty - 5)}
              >
                <Text style={styles.stepText}>−</Text>
              </Pressable>
              <Text style={styles.setpoint}>{fanDuty}%</Text>
              <Pressable
                style={styles.stepBtn}
                disabled={conn !== 'connected'}
                onPress={() => void writeDuty(fanDuty + 5)}
              >
                <Text style={styles.stepText}>+</Text>
              </Pressable>
            </View>
          </>
        )}
      </View>

      <View style={styles.footer}>
        {conn === 'connected' ? (
          <Pressable style={styles.secondaryBtn} onPress={() => void cleanup().then(() => setConn('idle'))}>
            <Text style={styles.secondaryText}>Disconnect</Text>
          </Pressable>
        ) : (
          <Pressable style={styles.primaryBtn} disabled={busy} onPress={() => void scanAndConnect()}>
            {busy ? (
              <ActivityIndicator color="#fff" />
            ) : (
              <Text style={styles.primaryText}>
                {conn === 'error' ? 'Retry connect' : `Connect to ${DEVICE_NAME}`}
              </Text>
            )}
          </Pressable>
        )}
        {error ? <Text style={styles.error}>{error}</Text> : null}
        <Text style={styles.hint}>
          Needs a development build (expo-dev-client). Expo Go cannot use BLE.
        </Text>
      </View>
    </SafeAreaView>
  );
}

const styles = StyleSheet.create({
  safe: {
    flex: 1,
    backgroundColor: '#e8eef2',
    paddingHorizontal: 20,
  },
  header: {
    paddingTop: 24,
    paddingBottom: 12,
  },
  brand: {
    fontFamily: Platform.select({ ios: 'AvenirNext-Bold', android: 'sans-serif-condensed', default: 'System' }),
    fontSize: 36,
    letterSpacing: 1,
    color: '#0b1f2a',
  },
  subtitle: {
    fontSize: 16,
    color: '#3d5a68',
    marginTop: 2,
  },
  card: {
    backgroundColor: '#f7fafc',
    borderRadius: 16,
    padding: 18,
    marginBottom: 12,
    borderWidth: 1,
    borderColor: '#d5e0e6',
  },
  label: {
    fontSize: 13,
    color: '#5a7380',
    textTransform: 'uppercase',
    letterSpacing: 0.6,
    marginBottom: 8,
  },
  tempBig: {
    fontSize: 48,
    fontWeight: '700',
    color: '#0b1f2a',
  },
  meta: {
    marginTop: 6,
    color: '#3d5a68',
    fontSize: 15,
  },
  row: {
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'space-between',
    gap: 12,
  },
  setpoint: {
    fontSize: 28,
    fontWeight: '600',
    color: '#0b1f2a',
    minWidth: 110,
    textAlign: 'center',
  },
  stepBtn: {
    width: 52,
    height: 52,
    borderRadius: 12,
    backgroundColor: '#1c3a4a',
    alignItems: 'center',
    justifyContent: 'center',
  },
  stepText: {
    color: '#fff',
    fontSize: 28,
    fontWeight: '600',
  },
  modeBtn: {
    flex: 1,
    paddingVertical: 14,
    borderRadius: 12,
    borderWidth: 1,
    borderColor: '#1c3a4a',
    alignItems: 'center',
  },
  modeBtnActive: {
    backgroundColor: '#1c3a4a',
  },
  modeText: {
    color: '#1c3a4a',
    fontWeight: '600',
    fontSize: 16,
  },
  modeTextActive: {
    color: '#fff',
  },
  footer: {
    marginTop: 'auto',
    paddingBottom: 24,
    gap: 10,
  },
  primaryBtn: {
    backgroundColor: '#0b6e4f',
    borderRadius: 14,
    paddingVertical: 16,
    alignItems: 'center',
  },
  primaryText: {
    color: '#fff',
    fontSize: 16,
    fontWeight: '700',
  },
  secondaryBtn: {
    borderRadius: 14,
    paddingVertical: 16,
    alignItems: 'center',
    borderWidth: 1,
    borderColor: '#1c3a4a',
  },
  secondaryText: {
    color: '#1c3a4a',
    fontSize: 16,
    fontWeight: '600',
  },
  error: {
    color: '#a11',
    textAlign: 'center',
  },
  hint: {
    color: '#5a7380',
    fontSize: 12,
    textAlign: 'center',
  },
});
