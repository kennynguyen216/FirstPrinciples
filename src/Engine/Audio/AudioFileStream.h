#pragma once

#include "Engine/Audio/AudioFileDecoder.h"

#include <atomic>
#include <thread>

namespace FP
{
	class AudioFileStream
	{
	public:
		static std::unique_ptr<AudioFileStream> Open(const AudioClip& clip, bool loop, AudioResult& result);
		~AudioFileStream();
		uint32 Read(float* samples, uint32 frames);
		void SetLoop(bool loop) { m_Loop.store(loop); }
		bool Finished() const { return m_Finished.load(std::memory_order_acquire); }
		bool Failed() const { return m_Failed.load(std::memory_order_acquire); }
		uint64 BufferedFrames() const { return m_Write.load(std::memory_order_acquire) - m_Read.load(std::memory_order_acquire); }
		uint64 BufferBytes() const { return m_Ring.size() * sizeof(float); }
	private:
		void Produce();
		AudioFileDecoder m_Decoder;
		std::vector<float> m_Ring;
		uint64 m_Capacity = 0;
		int m_Channels = 0;
		std::atomic<uint64> m_Read{0}, m_Write{0};
		std::atomic<bool> m_Loop{false}, m_Finished{false}, m_Failed{false}, m_Stop{false};
		std::thread m_Worker;
	};
}
