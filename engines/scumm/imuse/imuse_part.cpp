/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */



#include "common/debug.h"
#include "common/textconsole.h"
#include "common/util.h"
#include "scumm/imuse/imuse_internal.h"
#include "scumm/scumm.h"

namespace Scumm {

////////////////////////////////////////
//
//  IMuse Part implementation
//
////////////////////////////////////////

Part::Part() {
	_slot = 0;
	_next = nullptr;
	_prev = nullptr;
	_mc = nullptr;
	_player = nullptr;
	_pitchbend = 0;
	_pitchbend_factor = 0;
	_transpose = 0;
	_transpose_eff = 0;
	_vol = 0;
	_vol_eff = 0;
	_detune = 0;
	_detune_eff = 0;
	_pan = 0;
	_pan_eff = 0;
	_on = false;
	_modwheel = 0;
	_pedal = false;
	_pri = 0;
	_pri_eff = 0;
	_chan = 0;
	_effect_level = 0;
	_chorus = 0;
	_percussion = 0;
	_bank = 0;
	_unassigned_instrument = false;
}

void Part::saveLoadWithSerializer(Common::Serializer &ser) {
	int num;
	if (ser.isSaving()) {
		num = (_next ? (_next - _se->_parts + 1) : 0);
		ser.syncAsUint16LE(num);

		num = (_prev ? (_prev - _se->_parts + 1) : 0);
		ser.syncAsUint16LE(num);

		num = (_player ? (_player - _se->_players + 1) : 0);
		ser.syncAsUint16LE(num);
	} else {
		ser.syncAsUint16LE(num);
		_next = (num ? &_se->_parts[num - 1] : nullptr);

		ser.syncAsUint16LE(num);
		_prev = (num ? &_se->_parts[num - 1] : nullptr);

		ser.syncAsUint16LE(num);
		_player = (num ? &_se->_players[num - 1] : nullptr);
	}

	ser.syncAsSint16LE(_pitchbend, VER(8));
	ser.syncAsByte(_pitchbend_factor, VER(8));
	ser.syncAsSByte(_transpose, VER(8));
	ser.syncAsByte(_vol, VER(8));
	ser.syncAsSByte(_detune, VER(8));
	ser.syncAsSByte(_pan, VER(8));
	ser.syncAsByte(_on, VER(8));
	ser.syncAsByte(_modwheel, VER(8));
	ser.syncAsByte(_pedal, VER(8));
	ser.skip(1, VER(8), VER(16)); // _program
	ser.syncAsByte(_pri, VER(8));
	ser.syncAsByte(_chan, VER(8));
	ser.syncAsByte(_effect_level, VER(8));
	ser.syncAsByte(_chorus, VER(8));
	ser.syncAsByte(_percussion, VER(8));
	ser.syncAsByte(_bank, VER(8));
}

void Part::set_detune(int8 detune) {
	// Sam&Max does not have detune, so we just ignore this here. We still get
	// this called, since Sam&Max uses the same controller for a different
	// purpose.
	if (_se->_game_id == GID_SAMNMAX) {
#if 0
		if (_mc) {
			_mc->controlChange(17, detune + 0x40);
		}
#endif
	} else {
		_detune_eff = clamp((_detune = detune) + _player->getDetune(), -128, 127);
		sendPitchBend();
	}
}

void Part::pitchBend(int16 value) {
	_pitchbend = value;
	sendPitchBend();
}

void Part::volume(byte value) {
	_vol_eff = ((_vol = value) + 1) * _player->getEffectiveVolume() >> 7;
	if (_mc)
		_mc->volume(_vol_eff);
}

void Part::set_pri(int8 pri) {
	_pri_eff = clamp((_pri = pri) + _player->getPriority(), 0, 255);
	if (_mc)
		_mc->priority(_pri_eff);
}

void Part::set_pan(int8 pan) {
	_pan_eff = clamp((_pan = pan) + _player->getPan(), -64, 63);
	sendPanPosition(_pan_eff + 0x40);
}

void Part::set_transpose(int8 transpose) {
	_transpose = transpose;

	if (_se->_isAmiga) {
		// The Amiga version does a check like this. While this is probably a bug (a signed int8 can never be 128),
		// the playback depends on this being implemented exactly like in the original driver. I found this bug with
		// the WinUAE debugger. I don't know whether this is an Amiga only thing...
		_transpose_eff = /*(_transpose == 128) ? 0 : */transpose_clamp(_transpose + _player->getTranspose(), -12, 12);
		sendTranspose();
	} else {
		_transpose_eff = (_transpose == -128) ? 0 : transpose_clamp(_transpose + _player->getTranspose(), -24, 24);
		sendPitchBend();
	}
}

void Part::sustain(bool value) {
	_pedal = value;
	if (_mc)
		_mc->sustain(value);
}

void Part::modulationWheel(byte value) {
	_modwheel = value;
	if (_mc)
		_mc->modulationWheel(value);
}

void Part::chorusLevel(byte value) {
	_chorus = value;
	if (_mc)
		_mc->chorusLevel(value);
}

void Part::effectLevel(byte value) {
	_effect_level = value;
	sendEffectLevel(value);
}

void Part::fix_after_load() {
	set_transpose(_transpose);
	volume(_vol);
	set_detune(_detune);
	set_pri(_pri);
	set_pan(_pan);
	sendAll();
}

void Part::pitchBendFactor(byte value) {
	if (value > 12)
		return;
	pitchBend(0);
	_pitchbend_factor = value;
	if (_mc && !(_player->isGM()))
		_mc->pitchBendFactor(value);
}

void Part::set_onoff(bool on) {
	if (_on != on) {
		_on = on;
		if (!on)
			off();
		if (!_percussion)
			_player->_se->reallocateMidiChannels(_player->getMidiDriver());
	}
}

void Part::set_instrument(byte *data) {
	if (_se->_pcSpeaker)
		_instrument.pcspk(data);
	else
		_instrument.adlib(data);

	if (clearToTransmit())
		_instrument.send(_mc);
}

void Part::load_global_instrument(byte slot) {
	_player->_se->copyGlobalInstrument(slot, &_instrument);
	if (clearToTransmit())
		_instrument.send(_mc);
}

void Part::noteOn(byte note, byte velocity) {
	if (!_on)
		return;

	MidiChannel *mc = _mc;

	// DEBUG
	if (_unassigned_instrument && !_percussion) {
		_unassigned_instrument = false;
		if (!_instrument.isValid()) {
			debug(0, "[%02d] No instrument specified", (int)_chan);
			return;
		}
	}

	if (mc && _instrument.isValid()) {
		mc->noteOn(note, velocity);
	} else if (_percussion) {
		mc = _player->getMidiDriver()->getPercussionChannel();
		if (!mc)
			return;

		// FIXME: The following is evil, EVIL!!! Either prev_vol_eff is
		// actually meant to be a member of the Part class (i.e. each
		// instance of Part keeps a separate copy of it); or it really
		// is supposed to be shared by all Part instances -- but then it
		// should be implemented as a class static var. As it is, using
		// a function level static var in most cases is arcane and evil.
		static byte prev_vol_eff = 128;
		if (_vol_eff != prev_vol_eff) {
			mc->volume(_vol_eff);
			prev_vol_eff = _vol_eff;
		}
		if ((note < 35) && (!_player->_se->isNativeMT32()))
			note = Instrument::_gmRhythmMap[note];

		mc->noteOn(note, velocity);
	}
}

void Part::noteOff(byte note) {
	if (!_on)
		return;

	MidiChannel *mc = _mc;
	if (mc) {
		mc->noteOff(note);
	} else if (_percussion) {
		mc = _player->getMidiDriver()->getPercussionChannel();
		if (mc)
			mc->noteOff(note);
	}
}

void Part::init() {
	_player = nullptr;
	_next = nullptr;
	_prev = nullptr;
	_mc = nullptr;
}

void Part::setup(Player *player) {
	_player = player;

	_percussion = (player->isMIDI() && _chan == 9); // true;
	_on = true;
	_pri_eff = player->getPriority();
	_pri = 0;
	_vol = 127;
	_vol_eff = player->getEffectiveVolume();
	_pan = clamp(player->getPan(), -64, 63);
	_transpose_eff = player->getTranspose();
	_transpose = 0;
	_detune = 0;
	_detune_eff = player->getDetune();
	_pitchbend_factor = 2;
	_pitchbend = 0;
	_effect_level = player->_se->isNativeMT32() ? 127 : 64;
	_instrument.clear();
	_unassigned_instrument = true;
	_chorus = 0;
	_modwheel = 0;
	_bank = 0;
	_pedal = false;
	_mc = nullptr;
}

void Part::uninit() {
	if (!_player)
		return;
	off();
	_player->removePart(this);
	_player = nullptr;
}

void Part::off() {
	if (_mc) {
		_mc->allNotesOff();
		_mc->release();
		_mc = nullptr;
	}
}

bool Part::clearToTransmit() {
	if (_mc)
		return true;
	if (_instrument.isValid())
		_player->_se->reallocateMidiChannels(_player->getMidiDriver());
	return false;
}

void Part::sendAll() {
	if (!clearToTransmit())
		return;

	if (!_player->isGM())
		_mc->pitchBendFactor(_pitchbend_factor);
	sendTranspose();
	sendPitchBend();
	_mc->volume(_vol_eff);
	_mc->sustain(_pedal);
	_mc->modulationWheel(_modwheel);
	sendPanPosition(_pan_eff + 0x40);

	if (_instrument.isValid())
		_instrument.send(_mc);

	// We need to send the effect level after setting up the instrument
	// otherwise the reverb setting for MT-32 will be overwritten.
	sendEffectLevel(_effect_level);

	_mc->chorusLevel(_chorus);
	_mc->priority(_pri_eff);
}

void Part::sendPitchBend() {
	if (!_mc)
		return;

	int16 bend = _pitchbend;
	// RPN-based pitchbend range doesn't work for the MT32,
	// so we'll do the scaling ourselves.
	if (_player->_se->isNativeMT32())
		bend = bend * _pitchbend_factor / 12;

	// We send the transpose value separately for Amiga (which is more like the original handles this).
	// Some rhythm instruments depend on this.
	int8 transpose = _se->_isAmiga ? 0 : _transpose_eff;

	if (_player->isGM())
		// This is the DOTT formula. Might not be exactly like this for SAM...
		bend = ((clamp(((bend * _pitchbend_factor) >> 6) + _detune_eff + (transpose << 7), -2048, 2047) + 0x800) << 2) - 0x2000;
	else
		bend = clamp(bend + (_detune_eff * 64 / 12) + (transpose * 8192 / 12), -8192, 8191);

	_mc->pitchBend(bend);
}

void Part::sendTranspose() {
	if (!_mc)
		return;

	// See comment above. The transpose function was never implemented into our other drivers,
	// since this seems to have been handled via pitchBend() instead. The original drivers do have
	// such functions.
	if (!_se->_isAmiga)
		return;

	_mc->transpose(_transpose_eff);
}

void Part::programChange(byte value) {
	_bank = 0;
	_instrument.program(value, _player->isMT32());
	if (clearToTransmit())
		_instrument.send(_mc);
}

void Part::set_instrument(uint b) {
	_bank = (byte)(b >> 8);
	if (_bank)
		error("Non-zero instrument bank selection. Please report this");
	// HACK: Horrible hack to allow tracing of program change source.
	// The Mac m68k versions of MI2 and Indy4 use a different program "bank"
	// when it gets program change events through the iMuse SysEx handler.
	// We emulate this by introducing a special instrument, which sets
	// the instrument via sysEx_customInstrument. This seems to be
	// exclusively used for special sound effects like the "spit" sound.
	if (g_scumm->isMacM68kIMuse()) {
		_instrument.macSfx(b);
	} else {
		_instrument.program((byte)b, _player->isMT32());
	}
	if (clearToTransmit())
		_instrument.send(_mc);
}

void Part::allNotesOff() {
	if (!_mc)
		return;
	_mc->allNotesOff();
}

void Part::sendPanPosition(uint8 value) {
	if (!_mc)
		return;

	// As described in bug report #1849 "MI2: Minor problems in native MT-32 mode"
	// the original iMuse MT-32 driver did revert the panning. So we do the same
	// here in our code to have correctly panned sound output.
	if (_player->_se->isNativeMT32())
		value = 127 - value;

	_mc->panPosition(value);
}

void Part::sendEffectLevel(uint8 value) {
	if (!_mc)
		return;

	// As described in bug report #1849 "MI2: Minor problems in native MT-32 mode"
	// for the MT-32 one has to use a sysEx event to change the effect level (rather
	// the reverb setting).
	if (_player->_se->isNativeMT32()) {
		if (value != 127 && value != 0) {
			warning("Trying to use unsupported effect level value %d in native MT-32 mode.", value);

			if (value >= 64)
				value = 127;
			else
				value = 0;
		}

		byte message[9];
		memcpy(message, "\x41\x00\x16\x12\x00\x00\x06\x00\x00", 9);
		message[1] = _mc->getNumber();
		message[7] = (value == 127) ? 1 : 0;
		message[8] = 128 - (6 + message[7]);
		_player->getMidiDriver()->sysEx(message, 9);
	} else {
		_mc->effectLevel(value);
	}
}

} // End of namespace Scumm
