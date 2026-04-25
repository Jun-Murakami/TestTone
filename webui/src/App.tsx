import { useEffect, useState } from 'react';
import { Box, Button, Paper, Tooltip, Typography } from '@mui/material';
import { CssBaseline, ThemeProvider } from '@mui/material';
import { juceBridge } from './bridge/juce';
import { useJuceComboBoxIndex, useJuceToggleValue } from './hooks/useJuceParam';
import { darkTheme } from './theme';
import { PresetSliderCombo, type PresetOption } from './components/PresetSliderCombo';
import { useHostShortcutForwarding } from './hooks/useHostShortcutForwarding';
import { useGlobalZoomGuard } from './hooks/useGlobalZoomGuard';
import { GlobalDialog } from './components/GlobalDialog';
import LicenseDialog from './components/LicenseDialog';
import './App.css';

const FREQUENCY_PRESETS: PresetOption[] = [
  { value: 20, label: '20' },
  { value: 40, label: '40' },
  { value: 50, label: '50' },
  { value: 60, label: '60' },
  { value: 100, label: '100' },
  { value: 440, label: '440' },
  { value: 1000, label: '1k' },
  { value: 2000, label: '2k' },
  { value: 10000, label: '10k' },
  { value: 20000, label: '20k' },
];

const DBFS_PRESETS: PresetOption[] = [
  { value: 0, label: '0' },
  { value: -0.1, label: '-0.1' },
  { value: -1, label: '-1' },
  { value: -3, label: '-3' },
  { value: -6, label: '-6' },
  { value: -12, label: '-12' },
  { value: -18, label: '-18' },
  { value: -20, label: '-20' },
  { value: -24, label: '-24' },
  { value: -40, label: '-40' },
];

// "1k" / "1.5k" / "20k" / "1000" のような Hz 入力を受ける。
const parseFrequency = (s: string): number | null => {
  const cleaned = s.trim().toLowerCase().replace(/hz/g, '').trim();
  if (!cleaned) return null;
  const m = cleaned.match(/^([\d.]+)\s*k$/);
  if (m) {
    const n = parseFloat(m[1]);
    return Number.isFinite(n) ? n * 1000 : null;
  }
  const n = parseFloat(cleaned);
  return Number.isFinite(n) ? n : null;
};

// 1000 以上は kHz 表記の "1k" / "1.5k" / "20k" に短縮、未満はそのままの Hz 数値。
//  単位（Hz）は入力欄の外側に PresetSliderCombo の `unit` で別途表示する。
const formatFrequency = (v: number): string => {
  if (v >= 1000) {
    const kHz = v / 1000;
    const text = kHz % 1 === 0
      ? kHz.toFixed(0)
      : kHz.toFixed(2).replace(/0+$/, '').replace(/\.$/, '');
    return `${text}k`;
  }
  return v.toFixed(v < 100 ? 1 : 0);
};

const formatDbfs = (v: number): string => {
  if (Math.abs(v - Math.round(v)) < 1e-6) return v.toFixed(0);
  return v.toFixed(1);
};

function App() {
  useHostShortcutForwarding();
  useGlobalZoomGuard();

  // TONE_TYPE: 0 = Sine, 1 = Pink Noise
  const { index: toneTypeIndex, setIndex: setToneType } = useJuceComboBoxIndex('TONE_TYPE');
  const isPinkNoise = toneTypeIndex === 1;

  const { value: isOn, setValue: setIsOn } = useJuceToggleValue('ON', false);
  const { value: chL, setValue: setChL } = useJuceToggleValue('CH_L', true);
  const { value: chR, setValue: setChR } = useJuceToggleValue('CH_R', true);

  // ホストから渡されているバスレイアウト。プラグイン側が channelLayoutChanged で通知してくる。
  //  既定は stereo（2）。mono（1）の時は R トグルを隠して L を単独 Mute スイッチとして扱う。
  const [numChannels, setNumChannels] = useState<number>(2);
  useEffect(() => {
    const id = juceBridge.addEventListener('channelLayoutChanged', (d: unknown) => {
      const obj = d as { numChannels?: number } | null;
      const n = typeof obj?.numChannels === 'number' ? obj.numChannels : 2;
      setNumChannels(n > 0 ? n : 2);
    });
    return () => juceBridge.removeEventListener(id);
  }, []);
  const isMonoBus = numChannels <= 1;

  // ネイティブへの "ready" 通知（マウント時 1 回）。
  useEffect(() => {
    juceBridge.whenReady(() => {
      juceBridge.callNative('system_action', 'ready');
    });
  }, []);

  // 右クリック抑制（DAW 操作の邪魔にならないよう、入力系要素は除外）。
  useEffect(() => {
    const onContextMenu = (e: MouseEvent) => {
      const t = e.target as HTMLElement | null;
      if (!t) return;
      if (t.closest('input, textarea, select, [contenteditable="true"], .allow-contextmenu')) return;
      if (import.meta.env.DEV) return;
      e.preventDefault();
    };
    window.addEventListener('contextmenu', onContextMenu, { capture: true });
    return () => window.removeEventListener('contextmenu', onContextMenu, { capture: true });
  }, []);

  const [licenseOpen, setLicenseOpen] = useState(false);

  return (
    <ThemeProvider theme={darkTheme}>
      <CssBaseline />
      <style>{`
        html, body, #root {
          -webkit-user-select: none;
          -ms-user-select: none;
          user-select: none;
        }
        input, textarea, select, [contenteditable="true"], .allow-selection {
          -webkit-user-select: text !important;
          -ms-user-select: text !important;
          user-select: text !important;
          caret-color: auto;
        }
      `}</style>

      <Box
        sx={{
          height: '100vh',
          display: 'flex',
          flexDirection: 'column',
          p: 2,
          pt: 0,
          overflow: 'hidden',
        }}
      >
        {/* タイトル行 */}
        <Box sx={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', px: 1, py: 0.5 }}>
          <Typography
            variant='body2'
            component='div'
            sx={{ flexGrow: 1, color: 'primary.main', fontWeight: 600, cursor: 'pointer' }}
            onClick={() => setLicenseOpen(true)}
            title='Licenses'
          >
            TestTone
          </Typography>
          <Typography
            variant='caption'
            color='text.secondary'
            onClick={() => setLicenseOpen(true)}
            sx={{ cursor: 'pointer' }}
            title='Licenses'
          >
            by Jun Murakami
          </Typography>
        </Box>

        <Paper
          elevation={2}
          sx={{
            flex: 1,
            minHeight: 0,
            display: 'flex',
            flexDirection: 'column',
            px: 2,
            py: 2,
            gap: 2,
          }}
        >
          {/* 1 行目: Sine / Pink Noise トグル + 大きな On/Off ボタン */}
          <Box sx={{ display: 'flex', alignItems: 'center', gap: 1.5 }}>
            <Tooltip title='Tone source' arrow>
              <Box
                role='radiogroup'
                sx={{
                  display: 'inline-flex',
                  height: 28,
                  borderRadius: 1,
                  border: '1px solid',
                  borderColor: 'divider',
                  overflow: 'hidden',
                  fontSize: '0.8rem',
                  lineHeight: 1,
                  backgroundColor: 'background.paper',
                  flexShrink: 0,
                  userSelect: 'none',
                }}
              >
                {[
                  { idx: 0, label: 'Sine' },
                  { idx: 1, label: 'Pink Noise' },
                ].map((opt) => (
                  <Box
                    key={opt.idx}
                    role='radio'
                    aria-checked={toneTypeIndex === opt.idx}
                    onClick={() => setToneType(opt.idx)}
                    sx={{
                      px: 1.5,
                      display: 'flex',
                      alignItems: 'center',
                      cursor: 'pointer',
                      backgroundColor: toneTypeIndex === opt.idx ? 'primary.main' : 'transparent',
                      color: toneTypeIndex === opt.idx ? 'background.paper' : 'text.secondary',
                      fontWeight: toneTypeIndex === opt.idx ? 600 : 400,
                    }}
                  >
                    {opt.label}
                  </Box>
                ))}
              </Box>
            </Tooltip>

            <Box sx={{ flexGrow: 1 }} />

            {/* チャンネル個別ミュート。
                  - stereo バス時: L / R のセグメントトグル。両方点灯が既定。
                  - mono バス時:  R は意味を持たないので隠し、L 単独の Mute トグルとして表示。 */}
            <Box
              sx={{
                display: 'inline-flex',
                // ソースセレクタ（Sine / Pink Noise トグル）と同じ高さに揃える
                height: 28,
                borderRadius: 1,
                border: '1px solid',
                borderColor: 'divider',
                overflow: 'hidden',
                userSelect: 'none',
                fontSize: '0.8rem',
                lineHeight: 1,
              }}
            >
              {(isMonoBus
                ? [{ id: 'M', active: chL, set: setChL, withDivider: false }]
                : [
                    { id: 'L', active: chL, set: setChL, withDivider: true },
                    { id: 'R', active: chR, set: setChR, withDivider: false },
                  ]
              ).map((c) => (
                <Tooltip
                  key={c.id}
                  title={
                    isMonoBus
                      ? `Mono ${c.active ? 'ON' : 'MUTE'}`
                      : `${c.id}ch ${c.active ? 'ON' : 'MUTE'}`
                  }
                  arrow
                >
                  <Box
                    role='switch'
                    aria-checked={c.active}
                    onClick={() => c.set(!c.active)}
                    sx={{
                      width: 32,
                      display: 'flex',
                      alignItems: 'center',
                      justifyContent: 'center',
                      cursor: 'pointer',
                      fontWeight: c.active ? 700 : 400,
                      backgroundColor: c.active ? 'primary.main' : 'transparent',
                      color: c.active ? 'background.paper' : 'text.disabled',
                      borderRight: c.withDivider ? '1px solid' : 'none',
                      borderColor: 'divider',
                      transition: 'background-color 80ms, color 80ms',
                      '&:hover': {
                        backgroundColor: c.active ? 'primary.dark' : 'grey.800',
                        color: c.active ? 'background.paper' : 'text.primary',
                      },
                    }}
                  >
                    {c.id}
                  </Box>
                </Tooltip>
              ))}
            </Box>

            <Tooltip title={isOn ? 'Output is ON — signal is playing' : 'Output is OFF — silence'} arrow>
              <Button
                onClick={() => setIsOn(!isOn)}
                variant='contained'
                aria-pressed={isOn}
                sx={{
                  textTransform: 'none',
                  minWidth: 100,
                  height: 36,
                  fontSize: '0.95rem',
                  fontWeight: 700,
                  letterSpacing: 1,
                  border: '1px solid',
                  borderColor: isOn ? 'error.main' : 'divider',
                  backgroundColor: isOn ? 'error.main' : 'transparent',
                  color: isOn ? 'background.paper' : 'text.primary',
                  '&:hover': { backgroundColor: isOn ? 'error.dark' : 'grey.700' },
                }}
              >
                {isOn ? 'ON' : 'OFF'}
              </Button>
            </Tooltip>
          </Box>

          {/* 2 行目: Frequency（Pink Noise 時は無効）
              入力欄は数値だけ表示（Level 行と同様）、単位 "Hz" は欄の外右側に表示。 */}
          <PresetSliderCombo
            parameterId='FREQUENCY'
            label='Frequency'
            min={20}
            max={20000}
            skew='log'
            defaultValue={1000}
            unit='Hz'
            formatValue={formatFrequency}
            parseInput={parseFrequency}
            presets={FREQUENCY_PRESETS}
            disabled={isPinkNoise}
            wheelStep={2}
            wheelStepFine={0.5}
          />

          {/* 3 行目: dBFS */}
          <PresetSliderCombo
            parameterId='DBFS'
            label='Level'
            min={-90}
            max={0}
            skew='linear'
            defaultValue={-18}
            unit='dB'
            formatValue={formatDbfs}
            presets={DBFS_PRESETS}
            wheelStep={1}
            wheelStepFine={0.1}
          />
        </Paper>
      </Box>

      <LicenseDialog open={licenseOpen} onClose={() => setLicenseOpen(false)} />
      <GlobalDialog />
    </ThemeProvider>
  );
}

export default App;
