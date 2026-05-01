// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jun Murakami
import React, { useEffect, useRef, useState } from 'react';
import { Box, ButtonBase, Input, Slider, Typography } from '@mui/material';
import { useJuceSliderValue } from '../hooks/useJuceParam';
import { useFineAdjustPointer } from '../hooks/useFineAdjustPointer';

type SkewKind = 'linear' | 'log';

export interface PresetOption {
  value: number;
  label: string;
}

interface PresetSliderComboProps {
  parameterId: string;
  label: string;
  min: number;
  max: number;
  skew?: SkewKind;
  defaultValue?: number;
  /** スライダー値を表示文字列に整形する（数値入力欄の表示にも使う）。 */
  formatValue?: (v: number) => string;
  /** 入力欄のテキストを数値に解析する。返り値が null/NaN なら無視。
   *  既定では parseFloat。Hz の "1k"/"1.5k" のような短縮形をサポートしたい時に差し替える。 */
  parseInput?: (s: string) => number | null;
  /** スライダー右側の単位ラベル（"Hz" / "dB" など）。 */
  unit?: string;
  /** スライダー直下に並べるプリセットボタン。押すとその値にジャンプする。 */
  presets: PresetOption[];
  /** プリセット並び順を APVTS 値の昇順に強制ソートするか（既定 true）。
   *  false にすると渡した順番のまま並ぶ。 */
  sortPresets?: boolean;
  disabled?: boolean;
  /** wheel 1 tick の刻み（linear: 値空間、log: norm 空間 / 100 倍した比率） */
  wheelStep?: number;
  wheelStepFine?: number;
  inputWidth?: number;
  /** プリセットボタンが「アクティブ表示」になる許容誤差。値空間。 */
  presetEpsilon?: number;
}

const valueToNorm = (v: number, min: number, max: number, skew: SkewKind): number => {
  if (max === min) return 0;
  const c = Math.max(min, Math.min(max, v));
  if (skew === 'log' && min > 0) return Math.log(c / min) / Math.log(max / min);
  return (c - min) / (max - min);
};

const normToValue = (t: number, min: number, max: number, skew: SkewKind): number => {
  const c = Math.max(0, Math.min(1, t));
  if (skew === 'log' && min > 0) return min * Math.pow(max / min, c);
  return min + (max - min) * c;
};

export const PresetSliderCombo: React.FC<PresetSliderComboProps> = ({
  parameterId,
  label,
  min,
  max,
  skew = 'linear',
  defaultValue,
  formatValue,
  parseInput,
  unit,
  presets,
  sortPresets = true,
  disabled = false,
  wheelStep = 1,
  wheelStepFine = 0.2,
  inputWidth = 78,
  presetEpsilon,
}) => {
  const { value, state: sliderState, setScaled } = useJuceSliderValue(parameterId);
  const valueRef = useRef(value);
  valueRef.current = value;

  const [isDragging, setIsDragging] = useState(false);

  const applyValue = (v: number) => {
    if (disabled) return;
    setScaled(v, min, max);
  };

  const fmt = formatValue ?? ((v: number) => v.toFixed(1));
  const formatted = fmt(value);

  // 入力欄: フォーカスしていない時は formatted を表示（スライダー / プリセット連動）。
  //  フォーカス中は inputText を保持してタイピングを邪魔しない。
  const [inputText, setInputText] = useState<string>(formatted);
  const [isEditing, setIsEditing] = useState(false);
  const displayText = isEditing ? inputText : formatted;

  const parse = (s: string): number | null => {
    if (parseInput) return parseInput(s);
    const n = parseFloat(s);
    return Number.isFinite(n) ? n : null;
  };

  // wheel ホイール（スライダー本体に被せた Box で受ける）
  const wheelRef = useRef<HTMLDivElement | null>(null);
  useEffect(() => {
    const el = wheelRef.current;
    if (!el || disabled) return;
    const onWheel = (e: WheelEvent) => {
      e.preventDefault();
      const direction: 1 | -1 = -e.deltaY > 0 ? 1 : -1;
      const fine = e.shiftKey || e.ctrlKey || e.metaKey || e.altKey;
      const cur = valueRef.current;
      if (skew === 'log') {
        const s = (fine ? wheelStepFine : wheelStep) / 100;
        const n = valueToNorm(cur, min, max, 'log') + s * direction;
        applyValue(normToValue(n, min, max, 'log'));
      } else {
        const s = fine ? wheelStepFine : wheelStep;
        applyValue(cur + s * direction);
      }
    };
    el.addEventListener('wheel', onWheel, { passive: false });
    return () => el.removeEventListener('wheel', onWheel as EventListener);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [min, max, skew, wheelStep, wheelStepFine, disabled]);

  // 修飾キー + クリックで defaultValue リセット / ドラッグで微調整。
  const fineDragStartRef = useRef<{ value: number; norm: number }>({ value: 0, norm: 0 });
  const handlePointerDownCapture = useFineAdjustPointer({
    orientation: 'horizontal',
    onReset: () => {
      if (defaultValue !== undefined) applyValue(defaultValue);
    },
    onDragStart: () => {
      fineDragStartRef.current = {
        value: valueRef.current,
        norm: valueToNorm(valueRef.current, min, max, skew),
      };
      sliderState?.sliderDragStarted();
    },
    onDragDelta: (deltaPx) => {
      if (skew === 'log') {
        applyValue(normToValue(fineDragStartRef.current.norm + deltaPx * 0.002, min, max, 'log'));
      } else {
        applyValue(fineDragStartRef.current.value + deltaPx * wheelStepFine);
      }
    },
    onDragEnd: () => sliderState?.sliderDragEnded(),
  });

  // プリセット並び順（昇順 or 渡した順）。元配列を破壊しないようコピーしてからソート。
  const orderedPresets = sortPresets
    ? [...presets].sort((a, b) => a.value - b.value)
    : presets;

  // アクティブ判定の許容誤差。指定が無ければ「値域の 0.05% + プリセット間距離の 1%」程度を自動で。
  //  ピンポイント比較だと浮動小数誤差で外れたり、Sine 1000 Hz と "1 kHz" プリセットが微妙に
  //  ズレて全部非アクティブに見えたりするので、若干の余裕を持たせる。
  const eps = presetEpsilon ?? Math.max((max - min) * 0.0005, 1e-3);

  return (
    <Box
      sx={{
        display: 'grid',
        // 列: [ラベル / スライダー / プリセット] と [入力欄] と [単位] の 3 列。
        // 行 1: ラベル(左) と 入力欄+単位(右) を同じ行に並べる（横幅節約）。
        // 行 2: スライダーが 3 列ぶんを横断。
        // 行 3: プリセットボタンが 3 列ぶんを横断。
        gridTemplateColumns: `1fr ${inputWidth}px ${unit ? '20px' : '0px'}`,
        gridTemplateRows: 'auto auto auto',
        alignItems: 'center',
        columnGap: 0.75,
        rowGap: 0.25,
        width: '100%',
        opacity: disabled ? 0.45 : 1,
        pointerEvents: disabled ? 'none' : 'auto',
      }}
    >
      {/* ラベル: 行 1 左 */}
      <Typography
        variant='caption'
        sx={{
          gridRow: 1,
          gridColumn: 1,
          fontWeight: 600,
          fontSize: '0.78rem',
          letterSpacing: 0.5,
          color: 'text.primary',
          lineHeight: 1,
          pl: '6px', // スライダー thumb と左端を揃える
        }}
      >
        {label}
      </Typography>

      <Box
        ref={wheelRef}
        onPointerDownCapture={disabled ? undefined : handlePointerDownCapture}
        sx={{
          gridRow: 2,
          gridColumn: '1 / -1', // スライダーは行 2 で全列横断
          position: 'relative',
          display: 'flex',
          alignItems: 'center',
          minWidth: 0,
          px: '6px',
          py: 0,
        }}
      >
        <Slider
          disabled={disabled}
          value={valueToNorm(value, min, max, skew)}
          onChange={(_: Event, v: number | number[]) => {
            applyValue(normToValue(v as number, min, max, skew));
          }}
          onMouseDown={() => {
            if (!isDragging) {
              setIsDragging(true);
              sliderState?.sliderDragStarted();
            }
          }}
          onChangeCommitted={() => {
            if (isDragging) {
              setIsDragging(false);
              sliderState?.sliderDragEnded();
            }
          }}
          min={0}
          max={1}
          step={0.001}
          valueLabelDisplay='off'
          sx={{
            width: '100%',
            padding: 0,
            height: 12,
            '@media (pointer: coarse)': { padding: 0 },
            '& .MuiSlider-thumb': { width: 12, height: 12, transition: 'opacity 80ms' },
            '& .MuiSlider-track': { height: 3, border: 'none' },
            '& .MuiSlider-rail': { height: 3, opacity: 0.5 },
          }}
        />
      </Box>

      {/* 直接数値入力欄: 行 1 右（ラベルと同じ行の反対側） */}
      <Box
        sx={{
          gridRow: 1,
          gridColumn: 2,
          display: 'flex',
          alignItems: 'center',
          backgroundColor: '#252525',
          border: '1px solid #404040',
          borderRadius: 1,
          height: 20,
          overflow: 'hidden',
          justifySelf: 'end',
          width: '100%',
        }}
      >
        <Input
          className='block-host-shortcuts'
          value={displayText}
          onChange={(e) => setInputText(e.target.value)}
          onFocus={() => {
            setIsEditing(true);
            setInputText(formatted);
          }}
          onBlur={() => {
            setIsEditing(false);
            const n = parse(inputText);
            if (n !== null) applyValue(n);
          }}
          onKeyDown={(e) => {
            if (e.key === 'Enter') (e.target as HTMLInputElement).blur();
            if (e.key === 'Escape') {
              setInputText(formatted);
              (e.target as HTMLInputElement).blur();
            }
          }}
          disableUnderline
          sx={{
            flex: 1,
            minWidth: 0,
            '& input': {
              padding: '2px 6px',
              fontSize: '11px',
              textAlign: 'right',
              fontFamily: '"Red Hat Mono", monospace',
            },
          }}
        />
      </Box>

      {unit && (
        <Box sx={{ gridRow: 1, gridColumn: 3 }}>
          <Typography
            variant='caption'
            sx={{ fontSize: '11px', color: 'text.secondary', textAlign: 'left', lineHeight: 1 }}
          >
            {unit}
          </Typography>
        </Box>
      )}

      {/* プリセットボタン行: 行 3 で全列横断 */}
      <Box
        sx={{
          gridRow: 3,
          gridColumn: '1 / -1',
          display: 'flex',
          gap: 0.5,
          mt: 0.25,
          px: '6px', // スライダーの thumb 内側余白と揃える
        }}
      >
        {orderedPresets.map((p) => {
          const active = Math.abs(value - p.value) <= eps;
          return (
            <ButtonBase
              key={p.label}
              onClick={() => applyValue(p.value)}
              focusRipple
              sx={{
                flex: 1,
                minWidth: 0,
                height: 18,
                px: 0.5,
                borderRadius: 0.75,
                border: '1px solid',
                borderColor: active ? 'primary.main' : 'divider',
                backgroundColor: active ? 'primary.main' : 'transparent',
                color: active ? 'background.paper' : 'text.secondary',
                fontFamily: '"Red Hat Mono", monospace',
                fontSize: '10px',
                fontWeight: active ? 600 : 400,
                lineHeight: 1,
                whiteSpace: 'nowrap',
                overflow: 'hidden',
                textOverflow: 'ellipsis',
                transition: 'background-color 80ms, color 80ms',
                '&:hover': {
                  backgroundColor: active ? 'primary.dark' : 'rgba(255,255,255,0.06)',
                  color: active ? 'background.paper' : 'text.primary',
                },
              }}
            >
              {p.label}
            </ButtonBase>
          );
        })}
      </Box>
    </Box>
  );
};
