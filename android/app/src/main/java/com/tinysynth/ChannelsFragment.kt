package com.tinysynth

import android.os.Bundle
import android.view.Gravity
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.AdapterView
import android.widget.ArrayAdapter
import android.widget.LinearLayout
import android.widget.Spinner
import android.widget.TextView
import androidx.fragment.app.Fragment
import androidx.fragment.app.activityViewModels
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.lifecycleScope
import androidx.lifecycle.repeatOnLifecycle
import com.tinysynth.databinding.FragmentChannelsBinding
import kotlinx.coroutines.launch

class ChannelsFragment : Fragment() {
    private val viewModel: SynthViewModel by activityViewModels()
    private var binding: FragmentChannelsBinding? = null
    private val spinners = arrayOfNulls<Spinner>(16)
    private var instrumentEpoch = -1
    private var updating = false

    override fun onCreateView(inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?): View {
        val inflated = FragmentChannelsBinding.inflate(inflater, container, false)
        binding = inflated
        return inflated.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        ensureRows()
        viewLifecycleOwner.lifecycleScope.launch {
            viewLifecycleOwner.repeatOnLifecycle(Lifecycle.State.STARTED) {
                viewModel.state.collect { state -> bind(state) }
            }
        }
    }

    override fun onDestroyView() {
        binding = null
        spinners.fill(null)
        instrumentEpoch = -1
        super.onDestroyView()
    }

    private fun ensureRows() {
        val list = binding?.channelList ?: return
        if (spinners[0] != null) {
            return
        }
        for (channel in 0 until 16) {
            val row = LinearLayout(requireContext()).apply {
                orientation = LinearLayout.HORIZONTAL
                gravity = Gravity.CENTER_VERTICAL
                layoutParams = LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT,
                    LinearLayout.LayoutParams.WRAP_CONTENT
                ).also { it.bottomMargin = dp(8) }
            }
            val label = TextView(requireContext()).apply {
                text = if (channel == 9) {
                    getString(R.string.channel_label_drums, channel + 1)
                } else {
                    getString(R.string.channel_label, channel + 1)
                }
                textSize = 15f
                layoutParams = LinearLayout.LayoutParams(dp(156), LinearLayout.LayoutParams.WRAP_CONTENT)
            }
            val spinner = Spinner(requireContext()).apply {
                layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f)
            }
            val channelIndex = channel
            spinner.onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
                override fun onItemSelected(parent: AdapterView<*>?, view: View?, position: Int, id: Long) {
                    if (updating) {
                        return
                    }
                    viewModel.setChannelInstrument(channelIndex, position)
                }

                override fun onNothingSelected(parent: AdapterView<*>?) = Unit
            }
            row.addView(label)
            row.addView(spinner)
            list.addView(row)
            spinners[channel] = spinner
        }
    }

    private fun bind(state: UiState) {
        if (spinners[0] == null) {
            ensureRows()
        }
        if (state.instrumentEpoch != instrumentEpoch) {
            instrumentEpoch = state.instrumentEpoch
            val labels = ArrayList<String>(state.instruments.size + 1)
            labels.add(getString(R.string.from_midi_file))
            state.instruments.forEach { labels.add(it.displayName) }
            updating = true
            for (channel in 0 until 16) {
                val spinner = spinners[channel] ?: continue
                val adapter = ArrayAdapter(requireContext(), R.layout.item_instrument, ArrayList(labels))
                adapter.setDropDownViewResource(R.layout.item_instrument)
                spinner.adapter = adapter
                val selection = state.selections.getOrElse(channel) { 0 }.coerceIn(0, labels.lastIndex)
                spinner.setSelection(selection, false)
            }
            updating = false
        } else {
            updating = true
            for (channel in 0 until 16) {
                val spinner = spinners[channel] ?: continue
                val selection = state.selections.getOrElse(channel) { 0 }
                if (spinner.selectedItemPosition != selection && selection < spinner.count) {
                    spinner.setSelection(selection, false)
                }
            }
            updating = false
        }
    }

    private fun dp(value: Int): Int = (value * resources.displayMetrics.density).toInt()
}
