// Progression's natives (plugin/src/progress_natives.cpp). Keep the two in
// sync: tools/check-natives.py checks them with Natives.reds.

public native class TandemProgress extends IScriptable {
  // Our V's level (1..100), when it changes: the host keeps everyone's.
  public static native func SendOwnLevel(level: Int32)
  // Host: one of our NPCs' power level, scaling included.
  public static native func SendNpcLevel(npc: Uint32, level: Float)
  // Host: the PowerLevel modifier one of our NPCs should carry (0: none).
  // curveAtHost, curveAtSession: its content assignment's curve at the two
  // levels (< 0: none read); fixed: a constant content assignment.
  public static native func NpcLevelShift(level: Float, applied: Float, hostLevel: Float, sessionLevel: Float, curveAtHost: Float, curveAtSession: Float, fixed: Bool, maxLevel: Float) -> Float
  // Guest: the modifier a proxy should carry to be at the host's NPC's level.
  public static native func ProxyLevelShift(level: Float, applied: Float, hostNpcLevel: Float) -> Float
  // Cars (docs/architecture/garage.md). tandem.ini `lend` (0 crew, 1 only
  // with us aboard, 2 never), said to everyone; a partner's word (0 when it
  // said none); whether a partner may take the wheel under a word.
  public static native func OwnLend() -> Int32
  public static native func SendCarLend(lend: Int32)
  public static native func CarLendOf(player: Uint32) -> Int32
  public static native func MayDrive(lend: Int32, ownerAboard: Bool, ownerInSession: Bool) -> Bool
  // Vendors (docs/architecture/progression.md, Vendors.reds). Owner: one of
  // our NPCs is a vendor placed in the world (its static entity). The placed
  // entity of a shared NPC (0: none said); whether the session says that
  // entity's vendor died; the deaths heard since the last call (entity
  // hashes); a new session forgets the last one's.
  public static native func SendVendor(npc: Uint32, entity: EntityID)
  public static native func VendorOf(npc: Uint32) -> EntityID
  public static native func VendorDead(entity: EntityID) -> Bool
  public static native func TakeDeadVendors() -> array<Uint64>
  public static native func VendorsReset()
  // May our V trade with that vendor now (core/vendors.hpp)? 0 open, 1 not
  // a vendor, 2 no placed vendor, 3 out of reach, 4 dead, 5 not now, 6 not
  // in our world, 7 locked for us. Whether that answer shows the prompt;
  // its name, for the log; how close our V stands to trade.
  public static native func VendorShop(vendorRecord: Bool, entity: EntityID, distance: Float, proxyDead: Bool, sessionDead: Bool, free: Bool, ownFound: Bool, ownVendor: Bool, ownAlive: Bool, accessible: Bool) -> Int32
  public static native func VendorPrompts(shop: Int32) -> Bool
  public static native func VendorShopName(shop: Int32) -> String
  public static native func VendorReach() -> Float
  // tandem.ini `leave`: 0 stay, 1 save, 2 reload.
  public static native func LeaveMode() -> Int32
  // One step of our way out of a session (core/progress.hpp). left: what
  // of the session is still in our game (bits, Saves.reds); blocked: the
  // game's own save locks; saved: since we asked for the save, 0 no word,
  // 1 written, -1 failed. Returns 0 none, 1 clean (saving unlocks), 2 save
  // now, 3 saved, 4 load now, 5 the save failed, 6 gave up, 7 dirty.
  public static native func LeaveTick(now: Float, session: Bool, chosen: Bool, left: Int32, blocked: Bool, saved: Int32) -> Int32
  public static native func LeaveLocked() -> Bool
  // 0 solo, 1 session, 2 cleaning, 3 waiting, 4 saving, 5 loading, 6 dirty.
  public static native func LeaveStep() -> Int32
  // A world loaded: what the old one left is gone with it.
  public static native func LeaveReset()
  public static native func LeftNames(left: Int32) -> String
}
