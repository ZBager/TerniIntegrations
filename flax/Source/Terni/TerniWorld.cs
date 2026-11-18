// SPDX-License-Identifier: LGPL-2.1-only WITH AdditionRef-Terni-linking-exception
// Copyright (C) 2026 Rafał Zając
using System;

namespace Terni
{
    partial class TerniWorld
    {
        /// <summary>
        /// A subscribed field as of the last collected tick, without a copy. Valid until the
        /// tick after the next one begins: read it, do not keep it.
        /// </summary>
        /// <param name="id">The id Subscribe() returned.</param>
        public unsafe ReadOnlySpan<int> Subscribed(int id)
        {
            var data = GetSubscribedData(id, out var words);
            return data == IntPtr.Zero ? ReadOnlySpan<int>.Empty : new ReadOnlySpan<int>((void*)data, words);
        }

        /// <summary>
        /// The index of an entity type. Throws if the game has none of that name.
        /// </summary>
        public int Entity(string name)
        {
            var index = FindEntity(name);
            if (index < 0)
                throw new ArgumentException($"The game has no entity type '{name}'.");
            return index;
        }
    }

    partial struct TerniStageTime
    {
        /// <summary>
        /// GPU time per run, in milliseconds.
        /// </summary>
        public double AverageMs => Runs > 0 ? TotalMs / Runs : 0;
    }

    partial struct TerniContribution
    {
        /// <summary>
        /// The instance's handle, for SetModifierScale() and RemoveModifier().
        /// </summary>
        public TerniModifier Modifier => new TerniModifier { Slot = Slot, Version = Version };
    }

    /// <summary>
    /// Raw simulation values as numbers to show. The simulation itself only uses the raw
    /// integers: that is what makes it give the same result on every machine.
    /// </summary>
    public static class TerniValue
    {
        /// <summary>Raw 256 is 1.</summary>
        public const int QuantityOne = 1 << 8;
        /// <summary>Raw 65536 is 1.</summary>
        public const int RateOne = 1 << 16;

        /// <summary>
        /// A raw value as a number, for display.
        /// </summary>
        public static double ToDouble(int raw, TerniFieldType type)
        {
            switch (type)
            {
            case TerniFieldType.Quantity: return raw / (double)QuantityOne;
            case TerniFieldType.Rate: return raw / (double)RateOne;
            case TerniFieldType.Count:
            case TerniFieldType.Ref: return (uint)raw;
            default: return raw;
            }
        }

        /// <summary>
        /// A number as a raw value, rounded to the nearest step. In multiplayer, send the
        /// raw value to the other players, not the number it came from.
        /// </summary>
        public static int FromDouble(double value, TerniFieldType type)
        {
            switch (type)
            {
            case TerniFieldType.Quantity: return (int)Math.Round(value * QuantityOne);
            case TerniFieldType.Rate: return (int)Math.Round(value * RateOne);
            default: return (int)Math.Round(value);
            }
        }

        /// <summary>
        /// A money64 value from its two words.
        /// </summary>
        public static long ToInt64(int low, int high) => ((long)high << 32) | (uint)low;
    }
}
