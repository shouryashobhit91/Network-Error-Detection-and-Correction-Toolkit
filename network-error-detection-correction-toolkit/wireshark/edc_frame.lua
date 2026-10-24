-- edc_frame.lua - Wireshark dissector for the frames of the Error Detection and
-- Correction Toolkit (UDP ports 9100 and 9101, or any port by the "ED" magic).
--
-- Use: copy this file to the Wireshark personal Lua plugins folder
--      (Help > About Wireshark > Folders) and restart Wireshark, or
--      wireshark -X lua_script:edc_frame.lua    /    tshark -X lua_script:edc_frame.lua
-- Display filter examples:  edcf            edcf.type == 3 (NAK)
--                           edcf.hcrc_ok == 0 (frames whose header was damaged)

local p = Proto("edcf", "EDC Toolkit frame")

local types = { [1] = "DATA", [2] = "ACK", [3] = "NAK", [4] = "FIN", [5] = "FIN-ACK" }
local schemes = { [1] = "Single parity (even)", [2] = "Single parity (odd)", [3] = "Two-dimensional parity",
                  [4] = "Internet checksum", [5] = "CRC-8", [6] = "CRC-16-CCITT", [7] = "CRC-32",
                  [8] = "Hamming (7,4)", [9] = "Hamming SEC-DED (8,4)", [10] = "Hamming (7,4) + interleaving",
                  [11] = "SEC-DED + interleaving", [0] = "-" }

local f = p.fields
f.magic  = ProtoField.uint16("edcf.magic", "Magic", base.HEX)
f.ver    = ProtoField.uint8("edcf.version", "Version", base.DEC)
f.type   = ProtoField.uint8("edcf.type", "Type", base.DEC, types)
f.scheme = ProtoField.uint8("edcf.scheme", "Code", base.DEC, schemes)
f.flags  = ProtoField.uint8("edcf.flags", "Flags", base.HEX)
f.seq    = ProtoField.uint32("edcf.seq", "Sequence number", base.DEC)
f.dlen   = ProtoField.uint16("edcf.data_len", "Data length (bytes)", base.DEC)
f.cbits  = ProtoField.uint16("edcf.code_bits", "Codeword length (bits)", base.DEC)
f.hcrc   = ProtoField.uint16("edcf.hcrc", "Header CRC-16", base.HEX)
f.hok    = ProtoField.bool("edcf.hcrc_ok", "Header CRC correct")
f.cw     = ProtoField.bytes("edcf.codeword", "Codeword")

-- CRC-16-CCITT (poly 0x1021, init 0xFFFF) with plain arithmetic, so it works in
-- every Lua version that Wireshark has used
local function xor8(a, b)
    local r, m = 0, 1
    for _ = 1, 16 do
        if (a % 2) ~= (b % 2) then r = r + m end
        a, b, m = math.floor(a / 2), math.floor(b / 2), m * 2
    end
    return r
end
local function crc16(buf, len)
    local crc = 0xFFFF
    for i = 0, len - 1 do
        crc = xor8(crc, buf(i, 1):uint() * 256)
        for _ = 1, 8 do
            if crc >= 0x8000 then
                crc = xor8((crc * 2) % 0x10000, 0x1021)
            else
                crc = (crc * 2) % 0x10000
            end
        end
    end
    return crc
end

local function dissect(buf, pinfo, tree)
    if buf:len() < 16 or buf(0, 2):uint() ~= 0x4544 then return 0 end
    pinfo.cols.protocol = "EDC"
    local t = buf(3, 1):uint()
    local ok = crc16(buf, 14) == buf(14, 2):uint()
    local info
    if t == 2 or t == 3 or t == 5 then
        info = string.format("%s seq=%u", types[t] or "?", buf(6, 4):uint())
    else
        info = string.format("%s seq=%u code=%s data=%uB codeword=%ub", types[t] or "type " .. t, buf(6, 4):uint(),
                             schemes[buf(4, 1):uint()] or "?", buf(10, 2):uint(), buf(12, 2):uint())
    end
    if not ok then info = info .. "  [HEADER DAMAGED]" end
    pinfo.cols.info = info
    local st = tree:add(p, buf(), "EDC Toolkit frame: " .. info)
    st:add(f.magic, buf(0, 2)); st:add(f.ver, buf(2, 1)); st:add(f.type, buf(3, 1))
    st:add(f.scheme, buf(4, 1)); st:add(f.flags, buf(5, 1)); st:add(f.seq, buf(6, 4))
    st:add(f.dlen, buf(10, 2)); st:add(f.cbits, buf(12, 2))
    st:add(f.hcrc, buf(14, 2)); st:add(f.hok, ok)
    if buf:len() > 16 then st:add(f.cw, buf(16)) end
    return buf:len()
end

function p.dissector(buf, pinfo, tree) return dissect(buf, pinfo, tree) end
local udp = DissectorTable.get("udp.port")
udp:add(9100, p)
udp:add(9101, p)
p:register_heuristic("udp", function(buf, pinfo, tree)
    if buf:len() < 16 or buf(0, 2):uint() ~= 0x4544 then return false end
    dissect(buf, pinfo, tree)
    return true
end)
