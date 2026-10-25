#include "Engine/Audio/AudioFileStream.h"

#include <algorithm>
#include <array>
#include <chrono>

namespace FP
{
	std::unique_ptr<AudioFileStream> AudioFileStream::Open(const AudioClip& clip, bool loop, AudioResult& result) try
	{
		auto stream = std::unique_ptr<AudioFileStream>(new AudioFileStream);
		result = stream->m_Decoder.Open(clip.StreamPath);
		if (!result) return {};
		const auto& info = stream->m_Decoder.Info();
		if (info.SourceBytes != clip.SourceBytes || info.LastWriteTicks != clip.LastWriteTicks || info.Frames != clip.Frames || info.SampleRate != clip.SampleRate || info.Channels != clip.Channels)
		{
			result = {AudioError::DecodeFailed, "Streamed audio changed on disk; stop and reload the clip."}; return {};
		}
		stream->m_Capacity = info.SampleRate;
		stream->m_Channels = info.Channels;
		stream->m_Ring.resize(size_t(stream->m_Capacity * info.Channels));
		stream->m_Loop = loop;
		const auto frames = stream->m_Decoder.Read(stream->m_Ring.data(), stream->m_Capacity);
		if (!stream->m_Decoder.Result()) { result = stream->m_Decoder.Result(); return {}; }
		stream->m_Write.store(frames);
		stream->m_Worker = std::jthread([self = stream.get()](std::stop_token stop) { self->Produce(stop); });
		return stream;
	}
	catch (...)
	{
		result = {AudioError::BackendFailure, "Could not allocate a streaming buffer or start its decoder worker."};
		return {};
	}
	AudioFileStream::~AudioFileStream()
	{
		m_Worker.request_stop();
		if (m_Worker.joinable()) m_Worker.join();
	}
	void AudioFileStream::Produce(std::stop_token stop)
	{
		try
		{
			std::array<float, 4096> buffer{};
			while (!stop.stop_requested())
			{
				const uint64 write = m_Write.load(std::memory_order_relaxed);
				const uint64 room = m_Capacity - (write - m_Read.load(std::memory_order_acquire));
				if (!room) { std::this_thread::sleep_for(std::chrono::milliseconds(2)); continue; }
				const uint64 count = m_Decoder.Read(buffer.data(), std::min<uint64>(room, buffer.size() / m_Channels));
				if (!m_Decoder.Result()) { m_Failed.store(true, std::memory_order_release); return; }
				if (!count)
				{
					if (m_Loop.load() && m_Decoder.Rewind()) continue;
					if (!m_Decoder.Result()) m_Failed.store(true, std::memory_order_release);
					m_Finished.store(true, std::memory_order_release); return;
				}
				for (uint64 frame = 0; frame < count; ++frame)
					for (int channel = 0; channel < m_Channels; ++channel)
						m_Ring[((write + frame) % m_Capacity) * m_Channels + channel] = buffer[frame * m_Channels + channel];
				m_Write.store(write + count, std::memory_order_release);
			}
		}
		catch (...) { m_Failed.store(true, std::memory_order_release); }
	}
	uint32 AudioFileStream::Read(float* samples, uint32 frames)
	{
		const uint64 read = m_Read.load(std::memory_order_relaxed);
		const uint32 count = uint32(std::min<uint64>(frames, m_Write.load(std::memory_order_acquire) - read));
		for (uint32 frame = 0; frame < count; ++frame)
			for (int channel = 0; channel < m_Channels; ++channel)
				samples[frame * m_Channels + channel] = m_Ring[((read + frame) % m_Capacity) * m_Channels + channel];
		m_Read.store(read + count, std::memory_order_release);
		return count;
	}
}
