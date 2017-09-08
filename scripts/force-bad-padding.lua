-- Script for its use with MoonGen. Generates traffic with custom rate and
-- frame size.
-- Usage: Moongen fixed-rate.lua port rate(Gbps) framesize

local dpdk	= require "dpdk"
local memory	= require "memory"
local device	= require "device"
local dpdkc	= require "dpdkc"

local ffi	= require "ffi"

function master(txPort)
	if not txPort then
		errorf("usage: txPort")
	end

	local numQueues = 1

	rate = 5

	local txDev = device.config{ port = txPort, txQueues = numQueues }
	local ratePerQueue = 1000 * rate / numQueues

	for q = 1, numQueues do
		printf("Launch queue 1")
		local queue = txDev:getTxQueue(q - 1)
		queue:setRate(ratePerQueue)

		-- Launch sender threads
		dpdk.launchLua("loadSlave", txPort, q - 1)
	end

	dpdk.waitForSlaves()
end

function loadSlave(port, queue)
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
	end)

	local burst_size = 1082
	local firstburst_batchcount = 1962
	local firstburst_batches = 1000
	local secondburst_batchcount = 241
	local secondburst_batches = 4
	local death_packet_size = 1008

	local lastPrint = dpdk.getTime()
	local totalSent = 0
	local lastTotal = 0
	local lastSent = 0
	local firstburst = mem:bufArray(firstburst_batchcount)
	local secondburst = mem:bufArray(secondburst_batchcount)
	local final_frame = mem:bufArray(1)
	local buf = mem
	local seq = 0
	local size = 50
	local in_file = 0

	firstburst:alloc(burst_size)

	for i = 1, firstburst_batches do
		totalSent = totalSent + queue:send(firstburst)
	end

	in_file = (burst_size + 12) * totalSent

	printf("First burst end: Sent %d buffers, infile %d", totalSent, in_file)

	secondburst:alloc(burst_size)

	for i = 1, secondburst_batches do
		totalSent = totalSent + queue:send(secondburst)
	end

	in_file = (burst_size + 12) * totalSent

	printf("Second burst end: Sent %d buffers, infile %d", totalSent, in_file)

	final_frame:alloc(death_packet_size)

	totalSent = totalSent + queue:send(final_frame)
	in_file = (burst_size + 12) * totalSent

	printf("Death frame!!!! Sent %d buffers, infile %d", totalSent, in_file)

	dpdk.sleepMillis(2000, true)

	totalSent = totalSent + queue:send(final_frame)
	in_file = (burst_size + 12) * totalSent

	printf("Death frame 2!!!! Sent %d buffers, infile %d", totalSent, in_file)
	printf("If the bug is still there, losses will appear in 2 seconds")

	while dpdk.running() do
		totalSent = totalSent + queue:send(firstburst)
		local time = dpdk.getTime()

		if time - lastPrint > 1 then -- Update queue status each second
			local mpps = (totalSent - lastTotal) / (time - lastPrint) / 10^6
			printf("STATUS Q%d: Sent %d packets, current rate %.2f Mpps, %.2f Mbps", qidx, totalSent, mpps, mpps * size * 8)
			lastTotal = totalSent
			lastPrint = time
		end
	end
end

