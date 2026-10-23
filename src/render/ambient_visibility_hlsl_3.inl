            source += R"GZFX(                    indirectPixelOffset * FullInvSize;

                float2 positiveIndirectUv =
                    saturate(input.uv + indirectUvOffset);

                float2 negativeIndirectUv =
                    saturate(input.uv - indirectUvOffset);

                float positiveIndirectDepth =
                    DepthAt(positiveIndirectUv);

                float negativeIndirectDepth =
                    DepthAt(negativeIndirectUv);

                float sampleNormalization =
                    1.0 /
                    max((float)(sliceCount * stepCount * 2), 1.0);

                if (positiveIndirectDepth < 0.99999)
                {
                    float3 positivePosition = ReconstructView(
                        positiveIndirectUv,
                        positiveIndirectDepth);

                    float3 positiveDelta = positivePosition - center;
                    float positiveDistance = length(positiveDelta);

                    if (positiveDistance > 1e-4 &&
                        positiveDistance < indirectRadiusView)
                    {
                        float3 directionToSource =
                            positiveDelta / positiveDistance;

                        float receiverFacing =
                            saturate(dot(normal, directionToSource));

                        float distanceWeight = saturate(
                            1.0 - positiveDistance / indirectRadiusView);

                        distanceWeight *= distanceWeight;

                        float3 sourceColor = max(
                            SceneColor.SampleLevel(
                                LinearSampler,
                                positiveIndirectUv,
                                0).rgb,
                            0.0);

                        float sourceLuminance = Luminance(sourceColor);
                        float centerLuminance = Luminance(centerColor);

                        float excess = saturate(
                            (sourceLuminance - centerLuminance * 1.02) /
                            max(sourceLuminance, 0.05));

                        float weight =
                            receiverFacing * receiverFacing *
                            distanceWeight *
                            excess;

                        indirectSum +=
                            sourceColor *
                            weight *
                            sampleNormalization;

                        indirectNormalizer +=
                            weight *
                            sampleNormalization;
                    }
                }

                if (negativeIndirectDepth < 0.99999)
                {
                    float3 negativePosition = ReconstructView(
                        negativeIndirectUv,
                        negativeIndirectDepth);

                    float3 negativeDelta = negativePosition - center;
                    float negativeDistance = length(negativeDelta);

                    if (negativeDistance > 1e-4 &&
                        negativeDistance < indirectRadiusView)
                    {
                        float3 directionToSource =
                            negativeDelta / negativeDistance;

                        float receiverFacing =
                            saturate(dot(normal, directionToSource));

                        float distanceWeight = saturate(
                            1.0 - negativeDistance / indirectRadiusView);

                        distanceWeight *= distanceWeight;

                        float3 sourceColor = max(
                            SceneColor.SampleLevel(
                                LinearSampler,
                                negativeIndirectUv,
                                0).rgb,
                            0.0);

                        float sourceLuminance = Luminance(sourceColor);
                        float centerLuminance = Luminance(centerColor);

                        float excess = saturate(
                            (sourceLuminance - centerLuminance * 1.02) /
                            max(sourceLuminance, 0.05));

                        float weight =
                            receiverFacing * receiverFacing *
                            distanceWeight *
                            excess;

                        indirectSum +=
                            sourceColor *
                            weight *
                            sampleNormalization;

                        indirectNormalizer +=
                            weight *
                            sampleNormalization;
                    }
                }
            }
        }

        float baselineNegativeHorizon =
            -acos(clamp(lowNegative, -1.0, 1.0));

        float baselinePositiveHorizon =
            acos(clamp(lowPositive, -1.0, 1.0));

        float negativeHorizon =
            -acos(clamp(horizonNegative, -1.0, 1.0));

        float positiveHorizon =
            acos(clamp(horizonPositive, -1.0, 1.0));

        float baselineVisibility =
            projectedLength *
            (
                IntegrateArc(
                    baselineNegativeHorizon,
                    normalAngle,
                    cosNormal) +
                IntegrateArc(
                    baselinePositiveHorizon,
                    normalAngle,
                    cosNormal)
            );

        float localVisibility =
            projectedLength *
            (
                IntegrateArc(
                    negativeHorizon,
                    normalAngle,
                    cosNormal) +
                IntegrateArc(
                    positiveHorizon,
                    normalAngle,
                    cosNormal)
            );

        float sliceOcclusion = saturate(
            (baselineVisibility - localVisibility) /
            max(abs(baselineVisibility), 1e-4));

        occlusionSum += sliceOcclusion;
    }

    // Occlusion is defined only as visibility lost relative to the
    // receiver's own unoccluded hemisphere. A flat surface therefore
    // produces exactly zero regardless of projection or viewing angle.
    float occlusion = saturate(
        occlusionSum /
        max((float)sliceCount, 1.0) *
        1.75);

    occlusion = saturate(
        (occlusion - AoBias) /
        max(1.0 - AoBias, 1e-4));

    occlusion = pow(occlusion, AoPower);

)GZFX";
