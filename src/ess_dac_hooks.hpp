void RunLoop() {
        PinThread(pin_core_, 0);
        flac_ax::RamFilePreloader preloader(flac_ax::DetectHostPreloadPolicy());
        DsdTrackSession dsd_session;
        FlacRingDecoder flac_dec(ring_, meta_);
        std::vector<std::int32_t> dop_chunk(2048U * 2U, 0);

        for (std::size_t idx = 0; idx < tracks_.size(); ++idx) {
            if (!g_running.load(std::memory_order_acquire)) {
                break;
            }

            const std::string& path = tracks_[idx];
            dsd_session.Close();
            preloader.RefreshWindow(tracks_, idx);
            const flac_ax::RamFilePreloader::Entry* mem_entry = preloader.Find(path);

            bool opened_dsd = false;
            if (mem_entry != nullptr && !mem_entry->bytes.empty()) {
                opened_dsd = dsd_session.TryOpenMemory(
                    mem_entry->bytes.data(), static_cast<std::uint64_t>(mem_entry->bytes.size()));
            }
            if (!opened_dsd) {
                opened_dsd = dsd_session.TryOpenFile(path);
            }

            if (opened_dsd && dsd_session.active) {
                meta_.channels.store(2U, std::memory_order_relaxed);
                meta_.bits_per_sample.store(24U, std::memory_order_relaxed);
                meta_.is_dop.store(true, std::memory_order_relaxed);
                if (meta_.sample_rate.exchange(dsd_session.info.dop_pcm_rate_hz,
                                               std::memory_order_acq_rel) != dsd_session.info.dop_pcm_rate_hz) {
                    meta_.format_changed.store(true, std::memory_order_release);
                }

                while (g_running.load(std::memory_order_acquire)) {
                    const std::size_t n_frames =
                        dsd_session.streamer.ReadDopFramesS32(dop_chunk.data(), 2048U);
                    if (n_frames == 0U) {
                        break;
                    }
                    std::size_t off = 0U;
                    while (off < n_frames && g_running.load(std::memory_order_acquire)) {
                        const std::size_t pushed =
                            ring_.WriteFrames(dop_chunk.data() + off * 2U, n_frames - off);
                        off += pushed;
                        if (pushed == 0U) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(1));
                        }
                    }
                }
                dsd_session.Close();
                continue;
            }

            if (flac_dec.init(path.c_str()) == ::FLAC__STREAM_DECODER_INIT_STATUS_OK) {
                flac_dec.process_until_end_of_stream();
                flac_dec.finish();
            } else {
                std::cerr << "[FLAC_AX] Skipping unreadable track: " << path << "\n";
            }
        }
    }
   
preload_policy (3).hpp
+ 2

Once those two pieces are in place, flac_ax, dsd_dop_reader.hpp, and preload_policy.hpp line up cleanly. Would you like to check src/visual/ or CMakeLists.txt next?   
preload_policy (3).hpp
+ 3
