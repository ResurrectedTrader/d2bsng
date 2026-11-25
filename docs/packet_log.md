# Packet log (1.14d)

The dev console's **Packets** tab lists the game packets the 1.14d client receives and sends, each decoded into named fields or shown as bytes. It is for chasing bugs that start in the packet stream - an item the client places somewhere the server did not (`docs/belt_pickup_race.md`), a unit the client keeps after the server removed it - where the question is which packets arrived, in what order, in which frame.

It is on by default; `-nopacketlog` turns it off for a launch (the tab is left out and the intercepts copy nothing).

## Where packets are seen

Two intercepts in `hooks/Intercepts.cpp` hand every packet to one observer (`intercepts::SetPacketObserver`), before any callback or script sees it:

| Direction | Site | Thread |
|---|---|---|
| S->C | P3, the client's dispatch of each queued packet (`OnGamePacketReceived`) | game thread |
| C->S | P4, inside `CLIENT_Send` (`OnGamePacketSent`) | the sending thread: the game thread, or a script's |

Both see one packet per call: the client has already split the server's stream into packets (`CLIENT_EnqueuePacketsFromLocalServer`, or the TCP receive path) before P3 dispatches them, and every send is one packet.

A packet a script injected is tagged `[script]`:

- `sendPacket` (`game::SendGamePacket`) runs `CLIENT_Send` inside an `intercepts::ScriptSendScope`, which marks the calling thread.
- `getPacket` (`game::ReceiveGamePacket`) only queues its buffer; the client dispatches it later through P3, interleaved with the server's packets. `intercepts::NoteScriptReceive` keeps a copy of the buffer, and P3 matches each dispatched packet against the queued buffers by its bytes, from where the buffer's previous packet ended. A server packet byte-identical to the next packet of a pending script buffer would be tagged too; at most 64 buffers wait, so one the client never dispatches (the game ended first) does not pin the queue. The copies are kept only while the observer is installed.

## The recorder and the tab

`console/PacketInspector.cpp` holds one recorder for the process. The observer copies each packet into a bounded queue (the last 10000 by default) under the recorder's mutex. That is all it does on the hot path unless a tab control asks for more. Decoding is lazy: an entry is parsed the first time something wants its text (the tab draws it, `Copy` copies it, `Log to console` logs it, `Hide handled` filters on it), and the line is cached on the entry.

The tab's controls:

| Control | Effect |
|---|---|
| Server / Client | Show received / sent packets. Both off by default, so a fresh tab costs nothing to draw. |
| Hex | Show every entry as bytes. |
| Hide handled | Show only packets no class decodes - to find what is still unmapped. |
| Log to console | Mirror each shown packet to the `packets.inspector` log as it arrives. This decodes on the packet's own thread. |
| Record | Write the shown packets' raw bytes under `packet_recordings/<tick>/{server,client}/` next to `d2bs.dll`, one file per batch. |
| History | How many packets the queue keeps. |
| Exclude / Include / Stack trace | Id lists (`0x95 0x96`, hex with or without `0x`): hide these, show only these, log the handling thread's stack for these. The default exclude list drops the per-frame chatter (pings, vitals, movement, unit updates). |
| Copy / Clear | Copy the shown lines to the clipboard; empty the queue. |

A **batch** is the packets of one direction handled in one game frame (`intercepts::GameFrame`, counted by the draw intercept). The line prefix shows the batch's parity, so one frame's packets read as a group; a recording file holds one batch, its packets back to back as they crossed the wire.

The id filters apply to both directions: an id names a different packet each way.

Lines look like this:

```
[S->C] [0] ItemAction[0x9c, 20](action=PutInBelt, component=16, itemId=0x2, flags=Identified|InStore|StartItem|CompactSave|JustSaved, format=101, mode=InBelt, bodyLocation=None, grid=(0, 0), page=-1, code=hp1)
[S->C] [0] UnitWarp[0x15, 11](unitType=Player, unitId=0x1, position=(5343, 4253), reassign=true)
[C->S] [1] RequestQuestData[0x40, 1] [script]
[S->C] [0x76,   6] 76 00 01 00 00 00
```

The last one is the Hex view (also how an id no class maps, or bytes that do not decode, are shown): the bytes, then the same tags.

## Packet classes

`backends/lod114d/packets/` has one class per mapped id, in `client/` and `server/`. Each derives `Packet` (`packets/Packet.h`): `ID` and `NAME` constants, `Size()`, typed fields, `Deserialize(BinaryReader&)` and `ToString()`. `Packet::FromBytes` checks the id byte, runs `Deserialize`, and rejects a packet whose parse did not consume exactly `Size()` bytes, so a class whose layout is wrong shows as bytes rather than as wrong fields.

Field layouts come from D2MOO's `D2PacketDef.h`; names come from D2MOO's D2Game senders and handlers (`D2GAME_PACKETS_SendPacket0x..`, `D2GAME_PACKETCALLBACK_Rcv0x..`) where it names them. Enum-valued fields use the contract's enums (`game::UnitType`, `Skill`, `Stat`, `State`, `LevelId`, `ItemMode`, ...) or the packet-only ones in `packets/Enums.h` (`ItemAction`, `EventMessageType`, `UiAction`), so they print by name.

Two size checks keep a class honest:

- every fixed-size class `static_assert`s `sizeof` of its D2MOO struct against its `SIZE`;
- `Registry.cpp` `static_assert`s every class against the game's own size table (D2MOO's `gServerPacketSizeTable114d` / `gClientPacketSizeTable114d`, copied from Game.exe `0x730AE8` / `0x730DC0`): a fixed-size class's `SIZE` must match its id's entry, and a variable-size class's id must be variable (-1) there.

1.14d differs from the 1.10f numbering D2MOO's struct names use in the system packets: the client's game-session packets move one id up (`CreateGame` is 0x67, `JoinGame` 0x68 with two extra constants, `Ping` 0x6D at 13 bytes), the server's from 0xAF up (`GameLeft` 0xB0, `GameListEntry` 0xB2, `SystemError` 0xB4), and both sides gain a Warden packet (S->C 0xAE, C->S 0x66). The classes carry the 1.14d ids; the D2MOO struct a class checks against keeps its own name.

Variable-size and bit-packed packets are written by hand: the item packets 0x9C / 0x9D decode the head of the item bitstream (`packets/ItemHeader.cpp`: flags, format, mode, position, code or ear, a compact gold pile's amount - the layout `ITEMS_SerializeItem` writes), the vitals 0x18 / 0x95 / 0x96 their bit fields, and the length-prefixed packets (0x16, 0x3E, 0xA8, 0xAA, 0xAC, ...) their fixed head, with the rest as bytes.

### Adding a packet

1. Add `packets/client/<Name>Packet.h` or `packets/server/<Name>Packet.h`, following a neighbour: `ID`, `NAME`, `SIZE` (or `IsVariableSize()` and a `Size()` worked out in `Deserialize`), the fields, `Deserialize`, `ToString`, and a `static_assert` against the D2MOO struct where there is one.
2. Add it to `lod114d.vcxproj`.
3. Run `python scripts/gen_packet_registry.py` to regenerate `packets/RegistryTable.inc` (`--check` fails when it is out of date). It refuses two classes with one id in one direction.
4. Build: the registry's size checks run at compile time.

## Cost

Off (`-nopacketlog`): the intercepts load one null pointer per packet.

On, with the tab closed or idle, each packet costs one mutex, one small allocation (its bytes) and a deque push. Measured in a 1.14d simulator run of a SoloPlay sorceress (one virtual hour, about 99000 packets, 27 a second): 715 TSC cycles a packet on average, about 0.17 us at 4.3 GHz - some 13 ms of CPU over the hour. The run's packet digests were identical with and without the logger.

Decoding, which `Log to console` and `Hide handled` do as each packet arrives and the tab does for the rows it draws, averaged about 4100 cycles (1 us) a packet over the same run. Recording is the expensive control: it creates a file per batch, about 0.3 ms a packet in the same run, so leave it off unless the files are wanted.

While the tab draws, it holds the recorder's mutex, so a packet arriving then waits for the draw to finish; with the Server and Client boxes off (the default) the draw touches no entries.