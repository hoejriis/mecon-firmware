// The Companion's management channel (docs/contract/MANAGEMENT_CHANNEL.md): the binding record and its snapshot on the
// DataStore's file system, and the native channel table seen through the fingerprints MgmtChannel compares.
#include "MeconIface.h"
#ifdef ENABLE_MECON_IFACE
#include "MyMesh.h"
#include <Utils.h>
#include <helpers/cli/CliPolicy.h>
#include <helpers/iface/MgmtFsStore.h>
#include <helpers/iface/Wipe.h>
#include <string.h>

namespace {
Iface::PathFsStore g_mgmt_record("/mgmt_channel");
Iface::PathFsStore g_mgmt_snap("/mgmt_snap");

// The public channel's key, MyMesh.cpp's PUBLIC_GROUP_PSK ("izOH6cXN6mrJ5e26oRXNcg==") decoded: everyone holds it, so
// it is never a management channel.
const uint8_t PUBLIC_KEY[16] = {0x8b, 0x33, 0x87, 0xe9, 0xc5, 0xcd, 0xea, 0x6a,
                                0xc9, 0xe5, 0xed, 0xba, 0xa1, 0x15, 0xcd, 0x72};

void fpOf(const uint8_t* key, size_t len, uint8_t out[Iface::MGMT_FP_BYTES]) {
  mesh::Utils::sha256(out, Iface::MGMT_FP_BYTES, key, (int)len);
}

size_t keyLen(const ChannelDetails& ch) {
  static const uint8_t ZERO[16] = {0};
  return memcmp(&ch.channel.secret[16], ZERO, 16) == 0 ? 16 : 32;   // as setChannel() hashes it
}
}  // namespace

void MeconIface::mgmtLoad() {
  g_mgmt_record.setFs(_mesh->_store->getPrimaryFS());
  g_mgmt_snap.setFs(_mesh->_store->getPrimaryFS());
  fpOf(PUBLIC_KEY, sizeof(PUBLIC_KEY), _public_fp);
}

Iface::MgmtHost MeconIface::mgmtHost() {
  _chan_host = channelHost();
  return Iface::MgmtHost{&_chan_host, &g_mgmt_record, &g_mgmt_snap, &_jobs, mgmtSlotFp, mgmtKeyFp, _public_fp,
                         mgmtReadSlot, confirmCable, confirmRecordCaller, confirmJobId,
                         (uint8_t)(1u << Cli::C_WRITE), this};
}

bool MeconIface::mgmtSlotFp(void* user, int slot, uint8_t out[Iface::MGMT_FP_BYTES]) {
  ChannelDetails ch;
  if (!static_cast<MeconIface*>(user)->_mesh->getChannel(slot, ch)) return false;
  static const uint8_t ZERO[32] = {0};
  const bool keyed = memcmp(ch.channel.secret, ZERO, sizeof(ch.channel.secret)) != 0;
  if (keyed) fpOf(ch.channel.secret, keyLen(ch), out);
  Iface::wipe(&ch, sizeof(ch));
  return keyed;
}

void MeconIface::mgmtKeyFp(void*, const uint8_t* key, uint8_t out[Iface::MGMT_FP_BYTES]) {
  fpOf(key, Iface::CHANNEL_SECRET_BYTES, out);
}

// For the snapshot only. A 256-bit key cannot be put back through the 128-bit operations, so it is refused here and
// the change is not started.
bool MeconIface::mgmtReadSlot(void* user, int slot, char name[Iface::CHANNEL_NAME_MAX + 1],
                              uint8_t key[Iface::CHANNEL_SECRET_BYTES]) {
  ChannelDetails ch;
  if (!static_cast<MeconIface*>(user)->_mesh->getChannel(slot, ch)) return false;
  const bool ok = keyLen(ch) == Iface::CHANNEL_SECRET_BYTES;
  if (ok) {
    memcpy(name, ch.name, Iface::CHANNEL_NAME_MAX);
    name[Iface::CHANNEL_NAME_MAX] = 0;
    memcpy(key, ch.channel.secret, Iface::CHANNEL_SECRET_BYTES);
  }
  Iface::wipe(&ch, sizeof(ch));
  return ok;
}

#endif
