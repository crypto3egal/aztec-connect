import type { RemoteAsset } from 'alt-model/types';
import { getAssetPreferredFractionalDigits } from 'alt-model/known_assets/known_asset_display_data';
import React from 'react';
import { formatBaseUnits } from '../../../../app';
import { Input } from '../../../../components';
import style from './amount_input.module.scss';

interface AmountInputProps extends React.ComponentProps<typeof Input> {
  asset: RemoteAsset;
  maxAmount: bigint;
}

export function AmountInput({ asset, onChangeValue, maxAmount, ...inputProps }: AmountInputProps) {
  const handleChangeValue = (value: string) => onChangeValue?.(value.replace(/[^0-9.]/g, ''));

  const handleMaxButton = () => {
    const maxValue = maxAmount
      ? formatBaseUnits(maxAmount, asset.decimals, { precision: getAssetPreferredFractionalDigits(asset.address) })
      : '0';
    handleChangeValue(maxValue);
  };

  return (
    <div className={style.content}>
      <Input {...inputProps} onChangeValue={handleChangeValue} />
      <button className={style.maxButton} onClick={handleMaxButton}>
        Max
      </button>
    </div>
  );
}
