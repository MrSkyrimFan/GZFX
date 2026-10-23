            source += R"GZFX(    float aoRadiusView = max(viewUnitsPerPixel * max(AoRadius, 1.0), 0.001);
    float indirectRadiusView = max(
        viewUnitsPerPixel * max(IndirectRadius, 1.0),
        0.001);

    uint sliceCount = SliceCount();
    uint stepCount = StepCount();
    float2 noise = Hash22(floor(input.position.xy));

    float visibility = 0.0;
    float3 indirectSum = 0.0;
    float indirectNormalizer = 0.0;

    [loop]
    for (uint sliceIndex = 0; sliceIndex < 6; ++sliceIndex)
    {
        if (sliceIndex >= sliceCount)
        {
            break;
        }

        float phi =
            ((float)sliceIndex + noise.x) /
            (float)sliceCount *
            GZFX_PI;

        float cosine = cos(phi);
        float sine = sin(phi);

        float2 screenDirection = float2(cosine, -sine);
        float3 sliceDirection = float3(cosine, sine, 0.0);

        float3 orthogonalDirection =
            sliceDirection -
            dot(sliceDirection, viewVector) * viewVector;

        float orthogonalLength = length(orthogonalDirection);
        if (orthogonalLength < 1e-5)
        {
            visibility += 1.0;
            continue;
        }

        orthogonalDirection /= orthogonalLength;
        float3 sliceAxis = normalize(cross(orthogonalDirection, viewVector));

        float3 projectedNormal =
            normal -
            sliceAxis * dot(normal, sliceAxis);

        float projectedLength = length(projectedNormal);
        if (projectedLength < 1e-5)
        {
            visibility += 1.0;
            continue;
        }

        float normalSign = sign(dot(orthogonalDirection, projectedNormal));
        float cosNormal = saturate(
            dot(projectedNormal, viewVector) /
            projectedLength);

        float normalAngle = normalSign * acos(cosNormal);
        float lowPositive = cos(normalAngle + GZFX_HALF_PI);
        float lowNegative = cos(normalAngle - GZFX_HALF_PI);
        float horizonPositive = lowPositive;
        float horizonNegative = lowNegative;

        [loop]
        for (uint stepIndex = 0; stepIndex < 4; ++stepIndex)
        {
            if (stepIndex >= stepCount)
            {
                break;
            }

            float sequence = frac(
                noise.y +
                ((float)sliceIndex +
                 (float)stepIndex * (float)sliceCount) *
                0.61803398875);

            float sampleFraction =
                ((float)stepIndex + sequence) /
                (float)stepCount;

            sampleFraction = pow(saturate(sampleFraction), 1.75);

            float minimumFraction =
                1.5 /
                max(max(AoRadius, IndirectRadius), 1.5);

            sampleFraction = max(sampleFraction, minimumFraction);

            if (AoEnabled != 0 && AoRadius > 0.0)
            {
                float2 aoPixelOffset = round(
                    screenDirection *
                    AoRadius *
                    sampleFraction);

                float2 aoUvOffset = aoPixelOffset * FullInvSize;
                float2 positiveAoUv = saturate(input.uv + aoUvOffset);
                float2 negativeAoUv = saturate(input.uv - aoUvOffset);
                float positiveAoDepth = DepthAt(positiveAoUv);
                float negativeAoDepth = DepthAt(negativeAoUv);

                if (positiveAoDepth < 0.99999)
                {
                    float3 positivePosition =
                        ReconstructView(positiveAoUv, positiveAoDepth);
                    float3 positiveDelta = positivePosition - center;
                    float positiveDistance = length(positiveDelta);

                    if (positiveDistance > 1e-4)
                    {
                        float positiveWeight = saturate(
                            (aoRadiusView - positiveDistance) /
                            max(aoRadiusView * 0.35, 1e-4));

                        float positiveCosine = dot(
                            positiveDelta / positiveDistance,
                            viewVector);

                        horizonPositive = max(
                            horizonPositive,
                            lerp(lowPositive, positiveCosine, positiveWeight));
                    }
                }

                if (negativeAoDepth < 0.99999)
                {
                    float3 negativePosition =
                        ReconstructView(negativeAoUv, negativeAoDepth);
                    float3 negativeDelta = negativePosition - center;
                    float negativeDistance = length(negativeDelta);

                    if (negativeDistance > 1e-4)
                    {
                        float negativeWeight = saturate(
                            (aoRadiusView - negativeDistance) /
                            max(aoRadiusView * 0.35, 1e-4));

                        float negativeCosine = dot(
                            negativeDelta / negativeDistance,
                            viewVector);

                        horizonNegative = max(
                            horizonNegative,
                            lerp(lowNegative, negativeCosine, negativeWeight));
                    }
                }
            }

            if (IndirectEnabled != 0 && IndirectRadius > 0.0)
            {
                float2 indirectPixelOffset = round(
                    screenDirection *
                    IndirectRadius *
                    sampleFraction);

                float2 indirectUvOffset =
)GZFX";
