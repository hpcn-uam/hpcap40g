-- Script for its use with MoonGen. Generates traffic with custom rate and
-- frame size.
-- Usage: Moongen fixed-rate.lua port rate(Gbps) framesize

local dpdk	= require "dpdk"
local memory	= require "memory"
local device	= require "device"
local dpdkc	= require "dpdkc"

local ffi	= require "ffi"

function master(txPort, rate, size)
	if not txPort or not rate or not size then
		errorf("usage: txPort rate(Gbps) bytes-per-frame")
	end

	rate = tonumberall(rate)
	size = tonumberall(size)

	local numQueues = 2

	if rate > 10 then
		numQueues = 4 -- Improve rate for high throughput
	end

	local txDev = device.config{ port = txPort, txQueues = numQueues }
	local ratePerQueue = 1000 * rate / numQueues

	for q = 1, numQueues do
		local queue = txDev:getTxQueue(q - 1)
		queue:setRate(ratePerQueue)

		-- Launch sender threads
		dpdk.launchLua("loadSlave", txPort, q - 1, size)
	end

	local tstart = dpdk.getTime()
	dpdk.waitForSlaves()
	local tend = dpdk.getTime()

	printf("Runtime %d seconds\n", tend - tstart);
end

function loadSlave(port, queue, size)
	local qidx = queue
	local queue = device.get(port):getTxQueue(queue)

	-- Allocate a memory pool for the buffers
	local mem = memory.createMemPool(function(buf)
		local data = ffi.cast("uint8_t*", buf.pkt.data)
		-- src/dst mac
		for i = 0, 11 do
			data[i] = 0xCA
		end
		-- eth type
		data[12] = 0x12
		data[13] = 0x34

		for i = 14,(size - 1) do
			data[i] = 0xDE
		end
	end)

	local lastPrint = dpdk.getTime()
	local totalSent = 0
	local lastTotal = 0
	local lastSent = 0
	local bufs = mem:bufArray()
	local seq = 0

	while dpdk.running() do
		bufs:alloc(size)
		totalSent = totalSent + queue:send(bufs)
		local time = dpdk.getTime()

		if time - lastPrint > 1 then -- Update queue status each second
			local mpps = (totalSent - lastTotal) / (time - lastPrint) / 10^6
			printf("STATUS Q%d: Sent %d packets, current rate %.2f Mpps, %.2f Mbps", qidx, totalSent, mpps, mpps * size * 8)
			lastTotal = totalSent
			lastPrint = time
		end
	end

	printf("END Q%d: Sent %d packets", qidx, totalSent)
end

