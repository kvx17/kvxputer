#include "eng_calc_help.h"

namespace tools::engcalc {

const char* help_text(Op op) {
    switch (op) {
    case Op::Subnet:
        return "IPv4/IPv6 subnet math.\n"
               "Fields: IP, prefix or mask. Mode chip: bare / mask / split / hosts / v6.\n"
               "Blank unused fields. Example: 192.168.1.10/24\n"
               "Keys: F/X fields · Z/C mode · Enter run · Tab help";
    case Op::BaseConvert:
        return "Convert integers between bases.\n"
               "Fields: value, from-base, to-base. Optional width.\n"
               "Example: FF 16 10\n"
               "Keys: F/X fields · Enter run · Tab help";
    case Op::ByteUnits:
        return "Byte size to human units.\n"
               "Fields: count, unit (B KB MB GiB…). Bare number = bytes.\n"
               "Example: 1.5 MiB\n"
               "Keys: F/X · Enter · Tab help";
    case Op::Resistor:
        return "Resistor color code ↔ ohms.\n"
               "Type color words (brown black red gold) or rev + ohms.\n"
               "Example: brown black red gold\n"
               "Keys: type colors · Enter · Tab help";
    case Op::LedResistor:
        return "Series resistor for an LED.\n"
               "Fields: supply V, LED Vf, current mA (default 20).\n"
               "Example: 5 2.0 20\n"
               "Keys: F/X · Enter · Tab help";
    case Op::VoltageDivider:
        return "Divider voltage or design R2.\n"
               "Mode: div / design / ratio. Fields: R1 R2 Vin or Vin Vout R1.\n"
               "Example: 10k 4.7k 5\n"
               "Keys: Z/C mode · F/X · Enter · Tab help";
    case Op::Baud:
        return "Bit time and frame time from baud.\n"
               "Fields: baud, bits/frame (default 10).\n"
               "Example: 115200\n"
               "Keys: F/X · Enter · Tab help";
    case Op::Dbm:
        return "dBm ↔ mW and optional gain.\n"
               "Fields: value, unit (dBm/mW/W), optional gain_dB.\n"
               "Example: 10 dbm 3\n"
               "Keys: F/X · Enter · Tab help";
    case Op::Epoch:
        return "Unix time ↔ date.\n"
               "Mode: s / ms / date. Fields follow the mode.\n"
               "Example: 1700000000\n"
               "Keys: Z/C mode · F/X · Enter · Tab help";
    case Op::Measure:
        return "Temp, length, or AWG wire.\n"
               "Mode: temp / len / awg. Enter value and unit.\n"
               "Example: temp 25 C\n"
               "Keys: Z/C · F/X · Enter · Tab help";
    case Op::Bitmask:
        return "Set bits or decode a hex mask.\n"
               "Mode: bits / hex. Width 8/16/32/64.\n"
               "Example: bits 8 0,3,7\n"
               "Keys: Z/C · F/X · Enter · Tab help";
    case Op::Crc:
        return "CRC-32 / CCITT / Modbus over hex or text.\n"
               "Mode chip picks algorithm; paste bytes or text.\n"
               "Example: crc32 deadbeef\n"
               "Keys: Z/C mode · Enter · Tab help";
    case Op::Units:
        return "Convert units (length, mass…).\n"
               "Fields: value, from, to. Or list [dim].\n"
               "Example: 10 ft m\n"
               "Keys: F/X · Enter · Tab help";
    case Op::OhmsLaw:
        return "Ohm's law / power. Put - for the unknown.\n"
               "Fields: V I R W (any one blank/-).\n"
               "Example: v=12 r=100\n"
               "Keys: F/X · Enter · Tab help";
    case Op::VoltageDrop:
        return "DC wire voltage drop.\n"
               "Fields: metal (cu/al), length, amps, volts, AWG.\n"
               "Example: cu 15 10 120 12\n"
               "Keys: F/X · Enter · Tab help";
    case Op::Motor:
        return "Motor power / torque / rpm.\n"
               "Fields: hp, torque, rpm, eff%. Use - for unknown.\n"
               "Example: 1 - 1800 85\n"
               "Keys: F/X · Enter · Tab help";
    case Op::Gas:
        return "Boyle / ideal gas helpers.\n"
               "Mode: boyle / ideal / air. Fill known values, - unknown.\n"
               "Example: boyle 1 2 2 -\n"
               "Keys: Z/C · F/X · Enter · Tab help";
    case Op::Flow:
        return "Pipe flow / volume / Darcy.\n"
               "Mode: q / dv / vol / darcy. Units mm, m/s, L/min.\n"
               "Example: q 20 1.5\n"
               "Keys: Z/C · F/X · Enter · Tab help";
    case Op::Parallel:
        return "Parallel resistors.\n"
               "Add one value per row (10k, 4.7k…). Enter runs.\n"
               "Example: 10k 10k 1k\n"
               "Keys: F/X rows · +/- add/remove · Enter · Tab help";
    case Op::RcTimer:
        return "RC time constant and charge time.\n"
               "Fields: R, C, optional charge %.\n"
               "Example: 10k 100n 90\n"
               "Keys: F/X · Enter · Tab help";
    case Op::Battery:
        return "Battery life from capacity and draw.\n"
               "Fields: mAh and mA (or hours).\n"
               "Example: 2000 100\n"
               "Keys: F/X · Enter · Tab help";
    case Op::Frequency:
        return "Frequency, period, wavelength.\n"
               "Fields: Hz or period or wave length [vf].\n"
               "Example: 433M\n"
               "Keys: F/X · Enter · Tab help";
    case Op::Pwm:
        return "PWM duty ↔ high time.\n"
               "Fields: freq, duty% or high-time.\n"
               "Example: 1k 25\n"
               "Keys: F/X · Enter · Tab help";
    case Op::Percentage:
        return "Percent of, change, or ratio.\n"
               "Mode: of / change / ratio.\n"
               "Example: 15% 200\n"
               "Keys: Z/C · F/X · Enter · Tab help";
    case Op::DateDiff:
        return "Days between dates or add days.\n"
               "Mode: diff / add. Dates YYYY-MM-DD.\n"
               "Example: 2024-01-01 2024-06-01\n"
               "Keys: Z/C · F/X · Enter · Tab help";
    case Op::TimeDiff:
        return "Time difference or add duration.\n"
               "Fields: HH:MM … or full date-times.\n"
               "Example: 14:30 18:45\n"
               "Keys: F/X · Enter · Tab help";
    case Op::TipSplit:
        return "Tip and split a bill.\n"
               "Fields: bill, tip%, people.\n"
               "Example: 50 15 3\n"
               "Keys: F/X · Enter · Tab help";
    case Op::Distance:
        return "Great-circle distance (km/mi).\n"
               "Fields: lat1 lon1 lat2 lon2.\n"
               "Example: 40.7 -74 34.05 -118.2\n"
               "Keys: F/X · Enter · Tab help";
    case Op::Reactance:
        return "XC / XL / resonance.\n"
               "Mode: c / l / rc / lc. Enter F, R, C, L as needed.\n"
               "Example: c 1k 100n\n"
               "Keys: Z/C · F/X · Enter · Tab help";
    case Op::Timer555:
        return "555 astable or monostable.\n"
               "Mode: astable / mono. Ra Rb C or R C.\n"
               "Example: 10k 10k 100n\n"
               "Keys: Z/C · F/X · Enter · Tab help";
    case Op::Entropy:
        return "Password entropy / crack time.\n"
               "Paste a password, or len N charset.\n"
               "Example: hunter2\n"
               "Keys: Enter · Tab help";
    case Op::HashId:
        return "Guess hash algorithm from a digest.\n"
               "Paste the hash string.\n"
               "Example: 64 hex chars or $2b$…\n"
               "Keys: Enter · Tab help";
    case Op::Jwt:
        return "Decode JWT header and payload (no verify).\n"
               "Paste header.payload.signature.\n"
               "Keys: Enter · Tab help";
    case Op::Xor:
        return "XOR cipher helper.\n"
               "Paste: key text   or   hex:0d text\n"
               "Keys: Enter · Tab help";
    case Op::CidrAgg:
        return "Aggregate contiguous CIDR blocks.\n"
               "One net/pfx per row.\n"
               "Example: 10.0.0.0/24 10.0.1.0/24\n"
               "Keys: F/X rows · +/- · Enter · Tab help";
    case Op::Oui:
        return "Guess vendor from a MAC or OUI.\n"
               "Field: MAC (preferred) or 24-bit OUI.\n"
               "Longer MA-M/MA-S/IAB need a full MAC. Private = unlisted name.\n"
               "Example: 24:0A:C4:11:22:33\n"
               "Keys: Enter · Tab help · F/X next field (Sym+F types F)";
    case Op::Fspl:
        return "Free-space path loss / link budget.\n"
               "Mode: loss / link. Freq + distance, or tx/rx gains.\n"
               "Example: 2.4G 1km\n"
               "Keys: Z/C · F/X · Enter · Tab help";
    case Op::Transfer:
        return "Transfer time from size and rate.\n"
               "Fields: size (GiB…), rate (Mbps…).\n"
               "Example: 1GiB 100Mbps\n"
               "Keys: F/X · Enter · Tab help";
    case Op::Kwh:
        return "Energy cost.\n"
               "Fields: watts/hours/price or kWh + price.\n"
               "Example: 1500W 4h 0.25\n"
               "Keys: F/X · Enter · Tab help";
    case Op::Coord:
        return "Decimal ↔ DMS coordinates.\n"
               "Fields: lat lon   or   d m s N d m s W\n"
               "Example: 40.7128 -74.006\n"
               "Keys: F/X · Enter · Tab help";
    case Op::HexView:
        return "Hex dump of text or hex:bytes.\n"
               "Paste text or hex:68656c6c6f\n"
               "Keys: Enter · Tab help";
    case Op::Count:
        break;
    }
    return "Enter values, then Enter. Tab opens help.";
}

const char* life_help_text(int game) {
    switch (game) {
    case 0:
        return "Yu-Gi-Oh life points.\n"
               "Z/C pick side · F +step · X -step\n"
               "1/2/3 set step 100/500/1000 · H half · R reset\n"
               "Tab help · Esc back";
    case 1:
        return "Magic life / poison.\n"
               "Z/C side · F/X change · P poison mode\n"
               "1/5 step · S 20/40 start · R reset\n"
               "Tab help · Esc back";
    case 2:
        return "Truco Argentino.\n"
               "Z/C lado · 1/2/3 suman · X -1 · T 15/30\n"
               "Tab help · Esc back";
    default:
        break;
    }
    return "Life counter. Tab help · Esc back";
}

const char* scientific_help_text() {
    return "Scientific expression calculator.\n"
           "Type sin(30)+sqrt(2), ans*pi, 2^10…\n"
           "DEG/RAD chip sets angle mode.\n"
           "↑ focuses ops strip · ← → Enter insert\n"
           "Enter evaluates · Tab help · Esc back";
}

} // namespace tools::engcalc
