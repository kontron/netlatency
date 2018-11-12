-- Copyright (c) 2018, Kontron Europe GmbH
-- All rights reserved.
-- 
-- Redistribution and use in source and binary forms, with or without
-- modification, are permitted provided that the following conditions are met:
-- 
-- 1. Redistributions of source code must retain the above copyright notice, this
--    list of conditions and the following disclaimer.
-- 
-- 2. Redistributions in binary form must reproduce the above copyright notice,
--    this list of conditions and the following disclaimer in the documentation
--    and/or other materials provided with the distribution.
-- 
-- THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
-- AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
-- IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
-- DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
-- FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
-- DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
-- SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
-- CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
-- OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
-- OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

netlatency_protocol = Proto("Netlatency", "Netlatency Protocol")
netlatency_protocol.fields = {}

local fds = netlatency_protocol.fields
fds.version = ProtoField.int8("netlatency.version", "Version", base.DEC)
fds.stream_id = ProtoField.int8("netlatency.stream_id", "Stream ID", base.DEC)
fds.sequence_number = ProtoField.int32("netlatency.sequence_number", "Sequence Number", base.DEC)
fds.interval = ProtoField.int16("netlatency.interval", "Interval", base.DEC)
fds.offset = ProtoField.int16("netlatency.offset", "Time offset", base.DEC)
fds.flags = ProtoField.int32("netlatency.flags", "Flags", base.DEC)
fds.flags_eos = ProtoField.bool("netlatency.flags.eos", "End Of Stream", 32, nil, 0x1)
fds.flags_small_mode = ProtoField.bool("netlatency.flags.sm", "Small Mode", 32, nil, 0x2)

function netlatency_protocol.dissector(buffer, pinfo, tree)
	length = buffer:len()
	if length == 0 then return end

	pinfo.cols.protocol = netlatency_protocol.name

	local subtree = tree:add(netlatency_protocol, buffer(), "Netlatency Protocol Data")
	subtree:add_le(fds.version,         buffer(0,1))
	subtree:add_le(fds.stream_id,       buffer(1,1))
	subtree:add_le(fds.sequence_number, buffer(2,4))
	subtree:add_le(fds.interval,        buffer(6,2))
	subtree:add_le(fds.offset,          buffer(8,2))
	local flags_buf = buffer(10,4)
	local flagstree = subtree:add(fds.flags, flags_buf)
	flagstree:add(fds.flags_eos, flags_buf)
	flagstree:add(fds.flags_small_mode, flags_buf)
end

local eth_type = DissectorTable.get("ethertype")
eth_type:add(0x0808, netlatency_protocol)
