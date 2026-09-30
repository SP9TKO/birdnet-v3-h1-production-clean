# Development status

The accepted formal state remains M1, M2 and M3. No M4 or M5 acceptance is claimed here.

The integrated H1 firmware source follows the M55 → U85 → M55 → U85 → M55 schedule and keeps the shared `[1,7,9,1280]` feature explicit. The current `runOnce()` route uses the reference frontend, `h1RunFrontend()`.

The integrated application and its diagnostics were exercised in earlier development work. Those observations are engineering characterization, not biological-accuracy evidence or a new physical qualification. The complete reference-versus-production M55 frontend comparison tooling is retained in the firmware and host client. Its current disposition is `CHARACTERIZATION_ONLY`; `promotion` is `FORBIDDEN` because no justified complete-frontend numerical tolerance has been frozen.

Fresh production and diagnostic firmware builds have not been revalidated against the sanitized source paths. Five exact local build payloads and the pinned Zephyr/vendor SDK environment are required; see [`firmware/h1/BUILD_INPUTS.md`](../../firmware/h1/BUILD_INPUTS.md). Build revalidation is required before further publication claims.

Current disposition: `PUBLIC_TREE_SANITIZED` / `BUILD_REVALIDATION_REQUIRED`.
