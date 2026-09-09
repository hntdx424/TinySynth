using TinySynth.Playback;

namespace TinySynth;

public sealed class MainForm : Form
{
    private readonly MidiPlaybackEngine _engine = new();
    private readonly ComboBox[] _channelBoxes = new ComboBox[16];
    private readonly System.Windows.Forms.Timer _clock = new() { Interval = 80 };

    private Button _openMidiButton = null!;
    private Button _playButton = null!;
    private Button _pauseButton = null!;
    private Button _stopButton = null!;
    private Button _soundFontButton = null!;
    private TrackBar _seekBar = null!;
    private Label _timeLabel = null!;
    private Label _midiPathLabel = null!;
    private Label _soundFontPathLabel = null!;

    private bool _seeking;
    private bool _updatingChannels;

    public MainForm()
    {
        Text = "TinySynth";
        Font = new Font("Segoe UI", 9F);
        MinimumSize = new Size(720, 500);
        Size = new Size(780, 540);
        StartPosition = FormStartPosition.CenterScreen;
        Padding = new Padding(10);

        BuildUi();
        BindEvents();
        RefreshInstrumentLists();
        UpdateStatus();
        UpdateTransport();
    }

    private void BuildUi()
    {
        var tabs = new TabControl
        {
            Dock = DockStyle.Fill,
            Padding = new Point(12, 6)
        };

        tabs.TabPages.Add(BuildPlayerTab());
        tabs.TabPages.Add(BuildChannelsTab());
        Controls.Add(tabs);
    }

    private TabPage BuildPlayerTab()
    {
        var page = new TabPage("Player") { Padding = new Padding(16) };

        var root = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            ColumnCount = 1,
            RowCount = 4
        };
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        root.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));

        var transport = new FlowLayoutPanel
        {
            AutoSize = true,
            WrapContents = false,
            Margin = new Padding(0, 0, 0, 12)
        };
        _openMidiButton = MakeButton("Open MIDI…", 120);
        _playButton = MakeButton("Play", 88);
        _pauseButton = MakeButton("Pause", 88);
        _stopButton = MakeButton("Stop", 88);
        transport.Controls.AddRange([_openMidiButton, _playButton, _pauseButton, _stopButton]);

        var seekRow = new TableLayoutPanel
        {
            Dock = DockStyle.Top,
            AutoSize = true,
            ColumnCount = 2,
            Margin = new Padding(0, 4, 0, 16)
        };
        seekRow.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        seekRow.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));

        _seekBar = new TrackBar
        {
            Dock = DockStyle.Fill,
            Minimum = 0,
            Maximum = 1000,
            TickStyle = TickStyle.None,
            Enabled = false,
            Margin = new Padding(0, 0, 8, 0),
            Height = 36
        };
        _timeLabel = new Label
        {
            AutoSize = true,
            Text = "0:00 / 0:00",
            TextAlign = ContentAlignment.MiddleRight,
            Anchor = AnchorStyles.Right,
            Padding = new Padding(0, 8, 0, 0)
        };
        seekRow.Controls.Add(_seekBar, 0, 0);
        seekRow.Controls.Add(_timeLabel, 1, 0);

        var paths = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            ColumnCount = 2,
            RowCount = 2,
            AutoSize = true
        };
        paths.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        paths.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        paths.Controls.Add(Caption("MIDI"), 0, 0);
        _midiPathLabel = PathLabel("No MIDI file loaded");
        paths.Controls.Add(_midiPathLabel, 1, 0);
        paths.Controls.Add(Caption("SoundFont"), 0, 1);
        _soundFontPathLabel = PathLabel("No SoundFont loaded");
        paths.Controls.Add(_soundFontPathLabel, 1, 1);

        var footer = new FlowLayoutPanel
        {
            Dock = DockStyle.Fill,
            FlowDirection = FlowDirection.RightToLeft,
            AutoSize = true,
            WrapContents = false,
            Margin = new Padding(0, 12, 0, 0)
        };
        _soundFontButton = MakeButton("SoundFont…", 130);
        footer.Controls.Add(_soundFontButton);

        root.Controls.Add(transport, 0, 0);
        root.Controls.Add(seekRow, 0, 1);
        root.Controls.Add(paths, 0, 2);
        root.Controls.Add(footer, 0, 3);
        page.Controls.Add(root);
        return page;
    }

    private TabPage BuildChannelsTab()
    {
        var page = new TabPage("Channel instruments") { Padding = new Padding(12) };

        var root = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            ColumnCount = 1,
            RowCount = 2
        };
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        root.RowStyles.Add(new RowStyle(SizeType.Percent, 100));

        var hint = new Label
        {
            AutoSize = true,
            MaximumSize = new Size(700, 0),
            Text = "Assign an instrument to any of the 16 MIDI channels. " +
                   "A selection remaps that channel’s program and bank for the current or next playback. " +
                   "Channel 10 is the standard drum channel.",
            Margin = new Padding(0, 0, 0, 10)
        };

        var scroll = new Panel
        {
            Dock = DockStyle.Fill,
            AutoScroll = true,
            BorderStyle = BorderStyle.FixedSingle,
            Padding = new Padding(8)
        };

        var grid = new TableLayoutPanel
        {
            Dock = DockStyle.Top,
            AutoSize = true,
            ColumnCount = 2,
            RowCount = 16
        };
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 150));
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));

        for (var i = 0; i < 16; i++)
        {
            var channel = i;
            var caption = i == 9
                ? $"Channel {i + 1}  (drums)"
                : $"Channel {i + 1}";

            grid.RowStyles.Add(new RowStyle(SizeType.Absolute, 32));
            grid.Controls.Add(new Label
            {
                Text = caption,
                AutoSize = true,
                Anchor = AnchorStyles.Left,
                TextAlign = ContentAlignment.MiddleLeft,
                Padding = new Padding(0, 6, 0, 0)
            }, 0, i);

            var box = new ComboBox
            {
                DropDownStyle = ComboBoxStyle.DropDownList,
                Dock = DockStyle.Fill,
                IntegralHeight = false,
                MaxDropDownItems = 16,
                Anchor = AnchorStyles.Left | AnchorStyles.Right
            };
            box.SelectedIndexChanged += (_, _) => OnChannelChanged(channel);
            _channelBoxes[i] = box;
            grid.Controls.Add(box, 1, i);
        }

        scroll.Controls.Add(grid);
        root.Controls.Add(hint, 0, 0);
        root.Controls.Add(scroll, 0, 1);
        page.Controls.Add(root);
        return page;
    }

    private void BindEvents()
    {
        _openMidiButton.Click += (_, _) => OpenMidi();
        _playButton.Click += (_, _) => Play();
        _pauseButton.Click += (_, _) =>
        {
            _engine.Pause();
            UpdateTransport();
        };
        _stopButton.Click += (_, _) =>
        {
            _engine.Stop();
            UpdateSeekUi();
            UpdateTransport();
        };
        _soundFontButton.Click += (_, _) => OpenSoundFont();

        _seekBar.MouseDown += (_, _) => _seeking = true;
        _seekBar.MouseUp += (_, _) =>
        {
            if (_seeking)
            {
                _engine.Seek(SliderToTime(_seekBar.Value));
            }

            _seeking = false;
            UpdateSeekUi();
        };

        _clock.Tick += (_, _) =>
        {
            if (_engine.EndOfSequence && !_engine.IsPaused)
            {
                _engine.Stop();
                UpdateSeekUi();
                UpdateTransport();
                return;
            }

            if (!_seeking)
            {
                UpdateSeekUi();
            }

            UpdateTransport();
        };
        _clock.Start();

        FormClosed += (_, _) =>
        {
            _clock.Stop();
            _engine.Dispose();
        };
    }

    private void OpenMidi()
    {
        using var dialog = new OpenFileDialog
        {
            Title = "Open MIDI file",
            Filter = "MIDI files (*.mid;*.midi)|*.mid;*.midi|All files (*.*)|*.*",
            FilterIndex = 1
        };

        if (dialog.ShowDialog(this) != DialogResult.OK)
        {
            return;
        }

        try
        {
            _engine.LoadMidi(dialog.FileName);
            UpdateStatus();
            UpdateSeekUi();
            UpdateTransport();
        }
        catch (Exception ex)
        {
            ShowError("Could not open MIDI file.", ex);
        }
    }

    private void OpenSoundFont()
    {
        using var dialog = new OpenFileDialog
        {
            Title = "Select SoundFont",
            Filter = "SoundFont (*.sf2;*.dls)|*.sf2;*.dls|SoundFont 2 (*.sf2)|*.sf2|Downloadable Sounds (*.dls)|*.dls|All files (*.*)|*.*",
            FilterIndex = 1
        };

        if (dialog.ShowDialog(this) != DialogResult.OK)
        {
            return;
        }

        try
        {
            _engine.LoadSoundFont(dialog.FileName);
            RefreshInstrumentLists();
            UpdateStatus();
            UpdateTransport();
        }
        catch (NotSupportedException ex)
        {
            MessageBox.Show(this, ex.Message, "Unsupported soundfont",
                MessageBoxButtons.OK, MessageBoxIcon.Information);
        }
        catch (Exception ex)
        {
            ShowError("Could not load SoundFont.", ex);
        }
    }

    private void Play()
    {
        try
        {
            _engine.Play();
            UpdateTransport();
        }
        catch (InvalidOperationException ex)
        {
            MessageBox.Show(this, ex.Message, "TinySynth",
                MessageBoxButtons.OK, MessageBoxIcon.Information);
        }
        catch (Exception ex)
        {
            ShowError("Playback failed.", ex);
        }
    }

    private void OnChannelChanged(int channel)
    {
        if (_updatingChannels)
        {
            return;
        }

        var item = _channelBoxes[channel].SelectedItem as InstrumentListItem;
        _engine.SetChannelOverride(channel, item?.Choice);
    }

    private void RefreshInstrumentLists()
    {
        _updatingChannels = true;
        try
        {
            var instruments = _engine.AvailableInstruments;
            foreach (var box in _channelBoxes)
            {
                var previous = (box.SelectedItem as InstrumentListItem)?.Choice;
                box.BeginUpdate();
                box.Items.Clear();
                box.Items.Add(InstrumentListItem.FromMidi);
                foreach (var instrument in instruments)
                {
                    box.Items.Add(new InstrumentListItem(instrument));
                }

                box.SelectedIndex = IndexOf(box, previous);
                box.EndUpdate();
            }
        }
        finally
        {
            _updatingChannels = false;
        }
    }

    private static int IndexOf(ComboBox box, InstrumentChoice? previous)
    {
        if (previous is not { } choice)
        {
            return 0;
        }

        for (var i = 0; i < box.Items.Count; i++)
        {
            if (box.Items[i] is InstrumentListItem item &&
                item.Choice is { } current &&
                current.Bank == choice.Bank &&
                current.Program == choice.Program)
            {
                return i;
            }
        }

        return 0;
    }

    private void UpdateStatus()
    {
        _midiPathLabel.Text = _engine.MidiPath ?? "No MIDI file loaded";
        _soundFontPathLabel.Text = _engine.SoundFontPath ?? "No SoundFont loaded";
        _seekBar.Enabled = _engine.HasMidi;
    }

    private void UpdateSeekUi()
    {
        var duration = _engine.Duration;
        var position = _engine.HasMidi ? _engine.Position : TimeSpan.Zero;
        if (position > duration)
        {
            position = duration;
        }

        if (!_seeking && duration > TimeSpan.Zero)
        {
            _seekBar.Value = (int)Math.Clamp(
                position.TotalSeconds / duration.TotalSeconds * _seekBar.Maximum,
                _seekBar.Minimum,
                _seekBar.Maximum);
        }
        else if (!_seeking)
        {
            _seekBar.Value = 0;
        }

        _timeLabel.Text = $"{FormatTime(position)} / {FormatTime(duration)}";
    }

    private void UpdateTransport()
    {
        var canPlay = _engine.HasMidi && _engine.HasSoundFont;
        _playButton.Enabled = canPlay && !_engine.IsPlaying;
        _pauseButton.Enabled = _engine.IsPlaying;
        _stopButton.Enabled = _engine.IsPlaying || _engine.IsPaused;
    }

    private TimeSpan SliderToTime(int value)
    {
        if (_engine.Duration <= TimeSpan.Zero)
        {
            return TimeSpan.Zero;
        }

        return TimeSpan.FromSeconds(_engine.Duration.TotalSeconds * value / _seekBar.Maximum);
    }

    private void ShowError(string title, Exception ex)
    {
        MessageBox.Show(this, ex.Message, title, MessageBoxButtons.OK, MessageBoxIcon.Error);
    }

    private static string FormatTime(TimeSpan value)
    {
        var total = Math.Max(0, (int)value.TotalSeconds);
        return $"{total / 60}:{total % 60:00}";
    }

    private static Button MakeButton(string text, int width) => new()
    {
        Text = text,
        Width = width,
        Height = 32,
        Margin = new Padding(0, 0, 8, 0),
        UseVisualStyleBackColor = true
    };

    private static Label Caption(string text) => new()
    {
        Text = text,
        AutoSize = true,
        ForeColor = SystemColors.GrayText,
        Padding = new Padding(0, 4, 16, 8)
    };

    private static Label PathLabel(string text) => new()
    {
        Text = text,
        AutoSize = false,
        Dock = DockStyle.Fill,
        AutoEllipsis = true,
        Padding = new Padding(0, 4, 0, 8)
    };

    private sealed class InstrumentListItem(InstrumentChoice? choice)
    {
        public static InstrumentListItem FromMidi { get; } = new(null);

        public InstrumentChoice? Choice { get; } = choice;

        public override string ToString() =>
            Choice is { } instrument ? instrument.DisplayName : "(from MIDI file)";
    }
}
