import { Buffer } from 'buffer';
import { registerRootComponent } from 'expo';

import App from './App';

// BLE payload encode/decode uses Buffer
(globalThis as typeof globalThis & { Buffer: typeof Buffer }).Buffer = Buffer;

// registerRootComponent calls AppRegistry.registerComponent('main', () => App);
// It also ensures that whether you load the app in Expo Go or in a native build,
// the environment is set up appropriately
registerRootComponent(App);
