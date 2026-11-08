// SPDX-License-Identifier: GPL-3.0-only
/// What lights the DDJ-400's LEDs: any mapping (TRIGDECK, an FM-1 cartridge) gives its controls' lights.
public protocol LEDSource {
    /// every lit control's light (buttons; the pads of the deck's current mode)
    func lights() -> [DDJControl: LightLevel]
    /// the pads' lights in a given mode, [deck][pad] (the hardware shows each mode's own LEDs when switched to it)
    func padLights(for mode: PadMode) -> [[LightLevel]]
    /// each deck's pad mode as last seen (its LEDs are sent first)
    func padMode(_ deck: Deck) -> PadMode
}

extension GrooveboxMapper: LEDSource {}
extension TrigMapper: LEDSource {}
