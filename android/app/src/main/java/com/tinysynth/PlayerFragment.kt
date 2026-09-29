package com.tinysynth

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.SeekBar
import androidx.activity.result.contract.ActivityResultContracts
import androidx.fragment.app.Fragment
import androidx.fragment.app.activityViewModels
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.lifecycleScope
import androidx.lifecycle.repeatOnLifecycle
import com.tinysynth.databinding.FragmentPlayerBinding
import kotlinx.coroutines.launch

class PlayerFragment : Fragment() {
    private val viewModel: SynthViewModel by activityViewModels()
    private var binding: FragmentPlayerBinding? = null
    private var seeking = false

    private val openMidi = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) {
            viewModel.openMidi(requireContext(), uri)
        }
    }

    private val openSoundFont = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) {
            viewModel.openSoundFont(requireContext(), uri)
        }
    }

    override fun onCreateView(inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?): View {
        val inflated = FragmentPlayerBinding.inflate(inflater, container, false)
        binding = inflated
        return inflated.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        val binding = binding ?: return
        binding.openMidi.setOnClickListener { openMidi.launch(arrayOf("*/*")) }
        binding.soundFont.setOnClickListener { openSoundFont.launch(arrayOf("*/*")) }
        binding.play.setOnClickListener { viewModel.play() }
        binding.pause.setOnClickListener { viewModel.pause() }
        binding.stop.setOnClickListener { viewModel.stop() }
        binding.seek.max = 1000
        binding.seek.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(seekBar: SeekBar, progress: Int, fromUser: Boolean) {
                if (!fromUser) {
                    return
                }
                val duration = viewModel.state.value.durationMs
                binding.time.text = formatPair(duration * progress / 1000L, duration)
            }

            override fun onStartTrackingTouch(seekBar: SeekBar) {
                seeking = true
            }

            override fun onStopTrackingTouch(seekBar: SeekBar) {
                val duration = viewModel.state.value.durationMs
                viewModel.seek(duration * seekBar.progress / 1000L)
                seeking = false
            }
        })

        viewLifecycleOwner.lifecycleScope.launch {
            viewLifecycleOwner.repeatOnLifecycle(Lifecycle.State.STARTED) {
                viewModel.state.collect { state -> bind(state) }
            }
        }
    }

    override fun onDestroyView() {
        binding = null
        super.onDestroyView()
    }

    private fun bind(state: UiState) {
        val binding = binding ?: return
        binding.midiName.text = state.midiName ?: getString(R.string.no_midi)
        binding.soundFontName.text = state.soundFontName ?: getString(R.string.no_soundfont)
        binding.play.isEnabled = state.playEnabled
        binding.pause.isEnabled = state.pauseEnabled
        binding.stop.isEnabled = state.stopEnabled
        binding.openMidi.isEnabled = !state.busy
        binding.soundFont.isEnabled = !state.busy
        binding.seek.isEnabled = state.seekEnabled
        if (!seeking) {
            val progress = if (state.durationMs <= 0L) {
                0
            } else {
                ((state.positionMs * 1000L) / state.durationMs).toInt().coerceIn(0, 1000)
            }
            binding.seek.progress = progress
            binding.time.text = formatPair(state.positionMs.coerceAtMost(state.durationMs), state.durationMs)
        }
    }

    private fun formatPair(positionMs: Long, durationMs: Long): String {
        return getString(R.string.time_pattern, formatTime(positionMs), formatTime(durationMs))
    }

    private fun formatTime(ms: Long): String {
        val totalSeconds = (ms.coerceAtLeast(0L) / 1000L).toInt()
        return "%d:%02d".format(totalSeconds / 60, totalSeconds % 60)
    }
}
