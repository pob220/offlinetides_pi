#!/usr/bin/env python3
"""Spatially blocked validation of global tide-model source selection.

This is an offline authoring tool.  It never writes source-model or gauge data
to an operational package.  Every target gauge is predicted without using any
record in its geographic block, and records at the same physical coordinate
are therefore always withheld together.
"""

import argparse
import json
import math
import pathlib
import statistics

import numpy as np


MAJORS = ("m2", "s2", "n2", "k2", "k1", "o1", "p1", "q1")


def weighted_mean(values, weights):
    total = float(np.sum(weights))
    if total <= 0:
        return math.nan
    return float(np.sum(values * weights) / total)


def percentile(values, fraction):
    ordered = sorted(values)
    return ordered[round((len(ordered) - 1) * fraction)]


def summarize(values):
    return {
        "count": len(values),
        "mean_m": statistics.fmean(values),
        "median_m": statistics.median(values),
        "p90_m": percentile(values, 0.90),
        "p95_m": percentile(values, 0.95),
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--evaluation", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument("--block-degrees", type=float, default=5.0)
    parser.add_argument("--model-radius-km", type=float, default=1200.0)
    parser.add_argument("--coastal-residual-radius-km", type=float,
                        default=250.0)
    parser.add_argument("--river-residual-radius-km", type=float,
                        default=100.0)
    parser.add_argument("--minimum-model-neighbours", type=int, default=12)
    parser.add_argument("--challenger-ratio", type=float, default=0.70,
                        help="maximum local-error ratio for replacing TPXO")
    parser.add_argument("--difficult-score-m", type=float, default=0.10,
                        help="local TPXO score requiring ensemble treatment")
    parser.add_argument("--full-strength-radius-km", type=float, default=10.0)
    parser.add_argument("--observation-anchor-gain", type=float, default=1.0)
    parser.add_argument("--residual-gain", type=float, default=1.0)
    args = parser.parse_args()

    source = json.loads(args.evaluation.read_text(encoding="utf-8"))
    stations = source["stations"]
    models = tuple(source["models"])
    count = len(stations)
    lat = np.radians(np.array([item["latitude"] for item in stations]))
    lon = np.radians(np.array([item["longitude"] for item in stations]))
    gauge_type = np.array([item["gauge_type"] for item in stations])
    valid_fraction = np.array([item["valid_fraction"] for item in stations])
    observation_count = np.array(
        [max(1, item["observation_count"]) for item in stations])
    quality = valid_fraction * np.clip(np.log10(observation_count) / 6.0,
                                       0.25, 1.0)
    block_y = np.floor(
        (np.degrees(lat) + 90.0) / args.block_degrees).astype(int)
    block_x = np.floor(
        (np.degrees(lon) + 180.0) / args.block_degrees).astype(int)

    observed = {name: np.full(count, np.nan + 1j * np.nan,
                              dtype=np.complex128) for name in MAJORS}
    predicted = {
        model: {name: np.full(count, np.nan + 1j * np.nan,
                              dtype=np.complex128) for name in MAJORS}
        for model in models
    }
    sigma = {name: np.full(count, 0.25) for name in MAJORS}
    for index, station in enumerate(stations):
        for model in models:
            constituents = station["model_results"][model].get(
                "constituents", {})
            for name in MAJORS:
                if name not in constituents:
                    continue
                record = constituents[name]
                predicted[model][name][index] = complex(
                    record["model_real_m"], record["model_imaginary_m"])
                if not np.isfinite(observed[name][index].real):
                    observed[name][index] = complex(
                        record["observed_real_m"],
                        record["observed_imaginary_m"])
                    sigma[name][index] = max(
                        0.001, record.get("observation_sigma_m", 0.25))

    global_errors = {}
    for regime in ("coastal", "river"):
        regime_mask = gauge_type == regime
        global_errors[regime] = {}
        for model in models:
            global_errors[regime][model] = {}
            for name in MAJORS:
                valid = (regime_mask & np.isfinite(observed[name].real) &
                         np.isfinite(predicted[model][name].real))
                errors = np.abs(predicted[model][name][valid] -
                                observed[name][valid])
                global_errors[regime][model][name] = (
                    float(np.median(errors)) if len(errors) else 0.5)

    methods = ("raw_preferred_model", "guarded_local_blend",
               "difficulty_gated_blend",
               "nearest_taper_residual",
               "preferred_kernel_residual",
               "best_local_model", "local_model_blend",
               "local_blend_plus_residual")
    station_errors = {method: [] for method in methods}
    common_method_names = tuple(
        [f"raw_{model}" for model in models] +
        ["equal_model_blend"] + list(methods))
    common_model_subset = {method: [] for method in common_method_names}
    by_regime = {
        method: {"coastal": [], "river": []} for method in methods
    }
    selected_counts = {model: 0 for model in models}
    evaluated = 0

    for target in range(count):
        same_block = ((block_y == block_y[target]) &
                      (block_x == block_x[target]))
        cosine = (np.sin(lat[target]) * np.sin(lat) +
                  np.cos(lat[target]) * np.cos(lat) *
                  np.cos(lon - lon[target]))
        distances = 6371.0088 * np.arccos(np.clip(cosine, -1.0, 1.0))
        training = ~same_block
        regime = gauge_type[target]
        constituent_errors = {method: [] for method in methods}
        for name in MAJORS:
            observation = observed[name][target]
            if not np.isfinite(observation.real):
                continue
            local_scores = {}
            for model in models:
                model_values = predicted[model][name]
                candidates = (training & (gauge_type == regime) &
                              np.isfinite(observed[name].real) &
                              np.isfinite(model_values.real) &
                              (distances <= args.model_radius_km))
                candidate_indices = np.flatnonzero(candidates)
                if len(candidate_indices) >= args.minimum_model_neighbours:
                    nearest = candidate_indices[
                        np.argsort(distances[candidate_indices])[:96]]
                    errors = np.abs(model_values[nearest] -
                                    observed[name][nearest])
                    weights = (quality[nearest] *
                               np.exp(-distances[nearest] / 500.0) /
                               np.maximum(sigma[name][nearest], 0.01))
                    local_scores[model] = math.sqrt(
                        weighted_mean(errors * errors, weights))
                else:
                    global_valid = (training & (gauge_type == regime) &
                                    np.isfinite(observed[name].real) &
                                    np.isfinite(model_values.real))
                    global_values = np.abs(
                        model_values[global_valid] -
                        observed[name][global_valid])
                    local_scores[model] = (
                        float(np.median(global_values))
                        if len(global_values) else
                        global_errors[regime][model][name])

            available = [model for model in models
                         if np.isfinite(predicted[model][name][target].real)]
            if not available:
                continue
            best = min(available, key=lambda model: local_scores[model])
            selected_counts[best] += 1
            best_prediction = predicted[best][name][target]
            constituent_errors["best_local_model"].append(
                abs(best_prediction - observation))

            preferred = "tpxo" if "tpxo" in available else best
            preferred_prediction = predicted[preferred][name][target]
            constituent_errors["raw_preferred_model"].append(
                abs(preferred_prediction - observation))
            challengers = [
                model for model in available if model != preferred and
                local_scores[model] <=
                args.challenger_ratio * local_scores[preferred]
            ]
            guarded = preferred_prediction
            if challengers:
                challenger = min(challengers,
                                 key=lambda model: local_scores[model])
                relative_gain = max(
                    0.0, 1.0 - local_scores[challenger] /
                    max(0.005, local_scores[preferred]))
                challenger_weight = min(0.65, relative_gain)
                guarded = ((1.0 - challenger_weight) * preferred_prediction +
                           challenger_weight *
                           predicted[challenger][name][target])
            constituent_errors["guarded_local_blend"].append(
                abs(guarded - observation))

            raw_weights = np.array([
                1.0 / max(0.005, local_scores[model]) ** 2
                for model in available
            ])
            # Prevent one sparse local score from becoming absolute authority.
            raw_weights = np.minimum(raw_weights,
                                     9.0 * np.median(raw_weights))
            model_weights = raw_weights / np.sum(raw_weights)
            blended = sum(weight * predicted[model][name][target]
                          for model, weight in zip(available, model_weights))
            constituent_errors["local_model_blend"].append(
                abs(blended - observation))
            difficulty_gated = (
                preferred_prediction
                if preferred == "tpxo" and
                local_scores[preferred] < args.difficult_score_m
                else blended)
            constituent_errors["difficulty_gated_blend"].append(
                abs(difficulty_gated - observation))

            nearest_corrected = preferred_prediction
            residual_radius = (args.river_residual_radius_km
                               if regime == "river" else
                               args.coastal_residual_radius_km)
            preferred_values = predicted[preferred][name]
            nearest_candidates = np.flatnonzero(
                training & (gauge_type == regime) &
                np.isfinite(observed[name].real) &
                np.isfinite(preferred_values.real) &
                (distances <= residual_radius))
            if len(nearest_candidates):
                nearest = nearest_candidates[
                    np.argmin(distances[nearest_candidates])]
                fraction = min(1.0, max(
                    0.0, (distances[nearest] -
                          args.full_strength_radius_km) /
                    max(1e-6, residual_radius -
                        args.full_strength_radius_km)))
                taper = (1.0 - fraction) ** 2
                anchor_fraction = max(
                    0.0, 1.0 - distances[nearest] /
                    max(1e-6, args.full_strength_radius_km))
                gain = (args.residual_gain + anchor_fraction *
                        (args.observation_anchor_gain - args.residual_gain))
                nearest_corrected += gain * taper * (
                    observed[name][nearest] - preferred_values[nearest])
            constituent_errors["nearest_taper_residual"].append(
                abs(nearest_corrected - observation))

            kernel_corrected = preferred_prediction
            kernel_candidates = np.flatnonzero(
                training & (gauge_type == regime) &
                np.isfinite(observed[name].real) &
                np.isfinite(preferred_values.real) &
                (distances <= residual_radius))
            if len(kernel_candidates):
                residual_array = (
                    observed[name][kernel_candidates] -
                    preferred_values[kernel_candidates])
                weights_array = (
                    quality[kernel_candidates] *
                    np.exp(-(distances[kernel_candidates] /
                             residual_radius) ** 2) /
                    np.maximum(0.01, sigma[name][kernel_candidates]))
                correction = (
                    np.sum(weights_array * residual_array) /
                    np.sum(weights_array))
                effective_count = (np.sum(weights_array) ** 2 /
                                   np.sum(weights_array ** 2))
                scatter = weighted_mean(
                    np.abs(residual_array - correction) ** 2, weights_array)
                noise = scatter / max(1.0, effective_count)
                signal = abs(correction) ** 2
                shrinkage = max(0.0, (signal - noise) /
                                max(signal, 1e-12))
                minimum_count = 2.0 if regime == "river" else 3.0
                if effective_count < minimum_count:
                    shrinkage *= effective_count / minimum_count
                nearest_distance = np.min(distances[kernel_candidates])
                taper_fraction = max(
                    0.0, (nearest_distance -
                          args.full_strength_radius_km) /
                    max(1e-6, residual_radius -
                        args.full_strength_radius_km))
                taper = max(0.0, 1.0 - taper_fraction) ** 2
                maximum_correction = max(0.01, local_scores[preferred])
                if abs(correction) > maximum_correction:
                    correction *= maximum_correction / abs(correction)
                kernel_corrected += taper * shrinkage * correction
            constituent_errors["preferred_kernel_residual"].append(
                abs(kernel_corrected - observation))

            residual_candidates = (training & (gauge_type == regime) &
                                   np.isfinite(observed[name].real) &
                                   (distances <= residual_radius))
            neighbour_indices = np.flatnonzero(residual_candidates)
            residuals = []
            residual_weights = []
            for neighbour in neighbour_indices:
                neighbour_available = [
                    (model, weight)
                    for model, weight in zip(available, model_weights)
                    if np.isfinite(predicted[model][name][neighbour].real)
                ]
                if not neighbour_available:
                    continue
                normalizer = sum(weight for _, weight in neighbour_available)
                neighbour_model = sum(
                    weight * predicted[model][name][neighbour]
                    for model, weight in neighbour_available) / normalizer
                residuals.append(observed[name][neighbour] - neighbour_model)
                residual_weights.append(
                    quality[neighbour] *
                    math.exp(-(distances[neighbour] / residual_radius) ** 2) /
                    max(0.01, sigma[name][neighbour]))
            corrected = blended
            if residuals and sum(residual_weights) > 0:
                correction = sum(
                    weight * residual for weight, residual in
                    zip(residual_weights, residuals)) / sum(residual_weights)
                weights_array = np.asarray(residual_weights)
                residual_array = np.asarray(residuals)
                effective_count = (np.sum(weights_array) ** 2 /
                                   np.sum(weights_array ** 2))
                scatter = weighted_mean(
                    np.abs(residual_array - correction) ** 2, weights_array)
                noise = scatter / max(1.0, effective_count)
                signal = abs(correction) ** 2
                shrinkage = max(0.0, (signal - noise) /
                                max(signal, 1e-12))
                minimum_count = 2.0 if regime == "river" else 3.0
                if effective_count < minimum_count:
                    shrinkage *= effective_count / minimum_count
                nearest_distance = min(distances[neighbour_indices])
                taper = max(0.0, 1.0 - nearest_distance / residual_radius) ** 2
                maximum_correction = max(
                    0.01, min(local_scores[model] for model in available))
                if abs(correction) > maximum_correction:
                    correction *= maximum_correction / abs(correction)
                corrected += taper * shrinkage * correction
            constituent_errors["local_blend_plus_residual"].append(
                abs(corrected - observation))

        if not constituent_errors["local_model_blend"]:
            continue
        evaluated += 1
        station_method_rss = {}
        for method in methods:
            rss = math.sqrt(sum(value * value
                                for value in constituent_errors[method]))
            station_method_rss[method] = rss
            station_errors[method].append(rss)
            by_regime[method][regime].append(rss)

        # Compare methods on exactly the same records.  Without this subset a
        # model with broader coastal coverage can look worse merely because it
        # is being scored at more difficult stations.
        common = all(
            np.isfinite(observed[name][target].real) and all(
                np.isfinite(predicted[model][name][target].real)
                for model in models)
            for name in MAJORS)
        if common:
            for model in models:
                common_model_subset[f"raw_{model}"].append(math.sqrt(sum(
                    abs(predicted[model][name][target] -
                        observed[name][target]) ** 2
                    for name in MAJORS)))
            equal_errors = []
            for name in MAJORS:
                equal_prediction = sum(
                    predicted[model][name][target] for model in models
                ) / len(models)
                equal_errors.append(abs(equal_prediction -
                                        observed[name][target]))
            common_model_subset["equal_model_blend"].append(math.sqrt(sum(
                value * value for value in equal_errors)))
            for method, rss in station_method_rss.items():
                common_model_subset[method].append(rss)

    result = {
        "schema": "xtidal-spatial-blocked-ensemble-validation",
        "schema_version": 1,
        "configuration": {
            "block_degrees": args.block_degrees,
            "model_radius_km": args.model_radius_km,
            "coastal_residual_radius_km":
                args.coastal_residual_radius_km,
            "river_residual_radius_km": args.river_residual_radius_km,
            "minimum_model_neighbours": args.minimum_model_neighbours,
            "challenger_ratio": args.challenger_ratio,
            "difficult_score_m": args.difficult_score_m,
            "full_strength_radius_km": args.full_strength_radius_km,
            "observation_anchor_gain": args.observation_anchor_gain,
            "residual_gain": args.residual_gain,
            "models": models,
            "constituents": MAJORS,
        },
        "station_records": count,
        "evaluated_station_records": evaluated,
        "selected_model_constituent_counts": selected_counts,
        "methods": {},
        "common_model_subset": {
            method: summarize(values)
            for method, values in common_model_subset.items() if values
        },
    }
    for method in methods:
        result["methods"][method] = summarize(station_errors[method])
        result["methods"][method]["by_regime"] = {
            regime: summarize(values)
            for regime, values in by_regime[method].items() if values
        }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n",
                           encoding="utf-8")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
