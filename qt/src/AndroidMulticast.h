#pragma once

// Adquiere/libera un WifiManager.MulticastLock en Android para poder recibir
// paquetes UDP broadcast/multicast (Android no los entrega sin este lock).
// En plataformas no-Android son no-ops.
namespace AndroidMulticast {
void acquire();
void release();
}
