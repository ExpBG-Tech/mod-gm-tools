// # Audio credits and distribution notices
//
// Acquired 2026-10-01. The exact source and derivative hashes, resource GUIDs,
// formats, measured durations, and edit ranges are in [audio-assets.json](audio-assets.json).
// The audio licenses below are separate from this repository's MIT code license.
// Preserve this file with source and addon distribution, including Workshop credits.
// No creator endorsement is implied. Source rights checks are documented admission
// evidence, not a guarantee of legal title or a completed quality review.
//
// ## Gunshot Sounds — Tabasco / Vincent Sevedge
//
// [Original creator's Gunshot Sounds pack](https://opengameart.org/content/gunshot-sounds).
// **Copyright (c) 2009 Vincent Sevedge.**
//
// The downloaded `sounds/creativecommons.txt` supplies:
//
// > Audio and Visual content is covered under the Creative Commons Attribution 3.0 Unported License
//
// [Creative Commons Attribution 3.0 Unported](https://creativecommons.org/licenses/by/3.0/).
// The asset page labels the pack CC0. EXPBG retains the supplied copyright and
// CC BY 3.0 notice and credits both the page's creator handle and the named
// copyright holder. The ZIP and license notice are retained in the private
// acquisition cache with hashes in the ledger. CC BY 3.0 permits sharing and
// adaptation, including source WAV and packaged addon distribution, subject to
// the attribution/license notices; do not impose restrictions on these assets
// that prevent the license's permitted uses.
//
// Current original: `cz.wav` (creator's target-shooting recording).
// Current derivatives: `EAS_CZ_Short.wav`, `EAS_CZ_Dense.wav`.
// Historical `sks.wav` derivatives `EAS_SKS_Short.wav` and `EAS_SKS_Dense.wav`
// were removed at the user's request on 2026-10-02.
// Changes: selected distinct takes; arithmetic stereo-to-mono sum; preserved
// 48 kHz rate; 2 ms start and 200 ms end tapers; attenuation for headroom;
// PCM16 encoding. Short/dense clips arrange different takes at new time offsets
// with overlapping decays. They are designed burst arrangements from related
// recordings, not independent recording sessions. No pitch shift was used.
// The creator warns of recorder overload; absence of full-scale digital rails
// does not establish the absence of analogue distortion. Audition remains open.
//
// ## Removed from current bank: Distant Gunfire — iainmccurdy
//
// [Distant Gunfire](https://freesound.org/people/iainmccurdy/sounds/842326/),
// by **iainmccurdy**, [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/).
// Creator describes two hunting shots recorded in Provence, France.
// Derivative: `EAS_Distant_Hunting_Reports.wav`. Acquisition is the asset page's
// public HQ MP3 preview, not its login-gated original 96 kHz WAV. Changes: decode
// and resample to 48 kHz; average L/R for mono; start/end tapers; attenuation;
// PCM16. The full preview's two-report sequence and duration are retained.
//
// ## Removed from current bank: Distant Gunshot 4 (Mono) — morganpurkis
//
// [Distant Gunshot 4 (Mono)](https://freesound.org/people/morganpurkis/sounds/384714/),
// by **morganpurkis**, [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/).
// Creator describes editing their microphone-hit recording in Audacity. This is
// a designed report effect, not recorded ordnance. Derivative:
// `EAS_Designed_Report.wav`. Acquired public HQ MP3 preview. Changes: decode and
// resample to 48 kHz mono; edge tapers; attenuation; PCM16. Float decoding retains
// MP3 reconstruction overshoots before attenuation rather than hard clipping them.
//
// ## Muffled Distant Explosion — NenadSimic
//
// [Muffled Distant Explosion](https://opengameart.org/content/muffled-distant-explosion),
// by **NenadSimic**, [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/).
// The [creator's Freesound entry](https://freesound.org/people/NenadSimic/sounds/149966/)
// also documents the log-drum pitch/reverb design. This is a designed explosion
// effect, not recorded ordnance. Derivative: `EAS_Explosion_LogDrum.wav`. Original
// WAV acquired through OpenGameArt. Changes: resample to 48 kHz; mono average;
// edge tapers; attenuation; PCM16. No further pitch changes.
//
// ## Explosion_01.wav — tommccann / Tom McCann
//
// [Explosion_01.wav](https://freesound.org/people/tommccann/sounds/235968/),
// by **tommccann / Tom McCann**, [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/).
// Creator describes white noise and microphone blowing combined with reverb in
// Audacity. Designed effect, not recorded ordnance. Derivative:
// `EAS_Explosion_Noise.wav`. Acquired public HQ MP3 preview. Changes: float decode
// and resample to 48 kHz; mono average; edge tapers; attenuation; PCM16. Preview
// reconstruction overshoots were attenuated before PCM16 quantization.
//
// ## Distant Explosion.wav — ecfike
//
// [Distant Explosion.wav](https://freesound.org/people/ecfike/sounds/132861/),
// by **ecfike**, [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/).
// Creator describes an Audacity design from walking on a wood floor. Designed
// effect, not recorded ordnance. Derivative: `EAS_Explosion_Wood.wav`.
// Acquired public HQ MP3 preview. Changes: decode and resample to 48 kHz; mono
// average; edge tapers; attenuation; PCM16.
//
// CC0 permits copying, adaptation and distribution of source, package and preview
// derivatives without required attribution. The credits above are retained as
// provenance and courtesy. Freesound licenses apply to the sound; the public
// preview copies are honestly labeled in the ledger, and no original-download
// login requirement was bypassed.
//
// ## Held sources and remaining quality gates
//
// War Sounds MX and EE_War Sounds remain user-selected reference recordings.
// Their underlying recording provenance is unresolved; neither contributes audio
// to this addon. SuperPhat's automatic-fire candidate is held because the origin
// of its described gun-echo layer is unspecified. None has an admitted derivative.
//
// No sound was played. Listening, native range/direction, close approach, voice
// coexistence, looping/repetition perception and overall palette acceptance remain
// unverified. The original library contained four firearm events (including related
// burst arrangements) and three independently sourced designed explosions. Current
// counts and source distinctions are recorded in the manifest; subsequent imported
// recordings do not retroactively establish real combat provenance.
//
// ## Vinny Sounds radio and jet derivatives — Vinuesa — APL-SA
//
// Selected full recordings from **Vinny - Sounds 1.1.2**, Workshop ID
// `61D358A07E15C5FE`, by **Vinuesa**:
// https://reforger.armaplatform.com/workshop/61D358A07E15C5FE-Vinny-Sounds
//
// These derivatives are distributed under **Arma Public License Share Alike (APL-SA)**:
// https://www.bohemia.net/community/licenses/arma-public-license-share-alike
// Current canonical license page: https://bohemia.net/en/licenses/arma-public-license-share-alike
//
// | Original file | Original SHA-256 | EXPBG derivative |
// |---|---|---|
// | radio_static.wav | 81a87565eef8ec6fd2641871dffea7085668cc4e02c395343eb32a3324b9b52a | EAS_Radio_Static.wav |
// | apache_chatter.wav | efe72a147fe72ff2b8e2a7b1f77012b1a89056badc04ebfd62e6d55940650d45 | EAS_Radio_Apache1.wav |
// | apache_chatter2.wav | 92377f6cc24ccaaa4a691c8ab5daa23f931adafb2c852af5a1b1a73026e7f2c9 | EAS_Radio_Apache2.wav |
// | jet_flyby1.wav | b5cd0438599688577d58fe70173f07b0e8418220d606de73dc537fd57013995f | EAS_Jet_Flyby1.wav |
// | jet_flyby2.wav | 869d5edafacc7a651e2a872f618b23c2609bdc45c6364ced732f7ef2984aa854 | EAS_Jet_Flyby2.wav |
// | russian_radio1.wav | f1bd3d703fee734a20b943b40c5d566e7163a0265bf08c984dd1980183236a0e | EAS_Radio_Russian1.wav |
// | russian_radio2.wav | 16ab93e363d3f710762ba0534e10a75b2a0b31cc5f270acefe8854add0e55d86 | EAS_Radio_Russian2.wav |
// | russian_radio3.wav | 5a67ccbb2ecf9fe5b4c5d412109a393c4da1ae9e3b426a95688513b0954aaa3a | EAS_Radio_Russian3.wav |
// | russian_radio4.wav | 4d3899ab90f616ef959bb679155d4503ea5c7669bde1d0e83f3f737fbe51f483 | EAS_Radio_Russian4.wav |
//
// | chinese_1.wav | 6923bb7eff5dced3ab254db6e4a05a87981b88e22203f2b6fd61f63015477f6e | EAS_Radio_Chinese1.wav |
// | arab_radiochatter.wav | ddb57052a3ae0681cc414f6238282070b1868ed824ef24b02ebc162ac36b53cc | EAS_Radio_ArabChatter.wav |
// | arab_radio1.wav | b4ea28a39d253cf25b75987cf171bd1a9582e3c87c39adffe1895ffb6d0510e0 | EAS_Radio_Arab1.wav |
//
// EXPBG modifications (2026-10-01 and 2026-10-02): silent miniaudio 1.71 mono conversion/resampling
// to 48 kHz PCM16, full recording durations retained to the nearest sample frame,
// 2 ms fade-in and 200 ms fade-out to zero, attenuation only for <= -9 dBFS peak.
// No pitch change, phrase cuts or audible review. Derivative hashes and measured
// levels are in the manifest. Original masters and the exact public grant/license
// snapshots are retained outside the repository with recorded hashes.
//
// Admission relies on the public APL-SA grant supplied for Vinny Sounds. Original
// recording authorship and upstream rights are **not independently verified**.
// The source description says that sounds were found; it does not establish original
// recording authorship. This is grant-based reuse, not a claim of independently
// cleared original recordings. No author contact or creator endorsement is implied.
// Vinny's original description credits **Game Master FX by bacon** as its basis.
// That historical project credit is preserved here; no GMFX APL-ND code, graphs or
// media are included, and the radio/jet implementation and graphs are EXPBG original work.
//
// Keep attribution, license links and modification notices with these derivatives.
// APL-SA requires noncommercial use, use only with Arma games, and share-alike
// licensing of adapted material under its terms. The combined addon distribution
// therefore carries these additional conditions and is **not MIT-only**. Original
// EXPBG code remains MIT; prior CC0 and CC BY assets retain their own licenses.
// Do not relicense their independent source files as APL-SA or treat the MIT grant
// as permission to sell or use the included APL-SA recordings outside Arma.
//
// ## Removed after user listening (2026-10-02)
//
// EAS_Mosin_Report, EAS_Distant_Hunting_Reports, EAS_Designed_Report and EAS_CZ_Report are removed from the current source package and generated sound bank. Historical attribution remains above for prior releases. New audition candidates are separate local research files and are not shipped until selected.
//
// ## JSRS SOUNDMOD 2025 unchanged recordings — LordJarhead / Dennis Kahl
//
// Copyright remains with **LordJarhead / Dennis Kahl**. [Creator's stable release](https://steamcommunity.com/sharedfiles/filedetails/?id=3407948300), [Arma Public License No Derivatives](https://www.bohemia.net/en/licenses/arma-public-license-nd). Preserve the license's disclaimer of warranties and attribution conditions. Source is supplied as-is without a warranty of title, fitness or non-infringement; no creator endorsement is implied.
//
// Acquired 2026-10-02 from a public package mirror identifying Workshop3407948300, with a bundled APL-ND notice. Both audio PBOs identify author LordJarhead/version4.0.0.13; top-level mod.cpp says4.0.0.12. Mirror authenticity against Steam and original recording authorship are not independently verified. Full evidence and caveats: [audio source options](AUDIO_SOURCE_OPTIONS.md).
//
// Independent collection files: EAS_JSRS_AK762_Distant.wav, EAS_JSRS_AK762_Far.wav, EAS_JSRS_AR556_Distant.wav, EAS_JSRS_AR556_Far.wav, EAS_JSRS_LMG762_Distant.wav, EAS_JSRS_LMG762_Far.wav, EAS_JSRS_DSHK_Distant.wav, EAS_JSRS_DSHK_Far.wav, EAS_JSRS_Grenade_Distant.wav, EAS_JSRS_Grenade_Far.wav, EAS_JSRS_Rocket_Distant.wav, EAS_JSRS_Rocket_Far.wav.
//
// Changes: necessary lossless WSS-to-WAV container conversion only. Original stereo PCM16 at44.1kHz, duration and every PCM sample retained. No gain baking, fades, resampling, downmix, trimming, layering or new burst arrangements. Original sample/WAV/PCM hashes appear in the manifest. Runtime-only playback attenuation is separate from the distributed files.
//
// These unchanged recordings remain **APL-ND**, separately from the addon APL-SA label and MIT code. Noncommercial Arma-only use, creator attribution, license/disclaimer notices and the restriction on distributing adaptations continue to apply. No DynaSound audio is included.
//
// ## Added recordings and folder organization — 2026-10-02
//
// The addon groups samples under `AmbientWarEffects/Firefight`,
// `AmbientWarEffects/Explosions`, `AmbientWarEffects/Jets`, `AmbientWarMix`,
// `RadioTransmissions`, `Crowd`, and `SoundEffects`. Moving a file preserves its
// resource GUID and audio bytes. Existing JSRS distant/far recordings now share the
// Distant selector; the original source filenames and provenance remain unchanged.
//
// Two additional unchanged JSRS files, `EAS_JSRS_Grenade_Near.wav` and
// `EAS_JSRS_Rocket_Near.wav`, retain the same LordJarhead / Dennis Kahl APL-ND terms
// and complete original stereo PCM. They are original close-distance variants,
// not pitch, gain or distance processing applied to a distant recording.
//
// Three complete Vinny Sounds recordings were added under the existing public
// APL-SA package grant: `CloseFirefight1.wav` becomes `EAS_Firefight_Near.wav`,
// `distantfirefight_1.5.wav` becomes `EAS_Firefight_Distant.wav`, and
// `distant_shelling.wav` becomes `EAS_Shelling_Distant.wav`.
// Source page: https://reforger.armaplatform.com/workshop/61D358A07E15C5FE
// These new derivatives use 24 kHz mono PCM16 with 10 ms start and 200 ms end ramps,
// full source duration retained to the nearest output sample, and attenuation only
// for headroom. The very quiet distant firefight was not amplified. Original
// shelling contains full-scale samples; conversion cannot repair source distortion.
// The source package's upstream authorship remains not independently verified.
//
// **Cheeseheadburger**, [Distant WW2 Gunfire Kent.wav](https://freesound.org/people/Cheeseheadburger/sounds/170478/),
// [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/), becomes
// `EAS_WarMix_Kent.wav`. The creator describes a reenactment recorded in a Kent wood
// in 2012 with Schoeps CMIT and 744. The acquired input is the public HQ MP3
// preview, not the original 24-bit WAV. Changes: complete recording decoded and
// resampled to 24 kHz mono PCM16, edge ramps and attenuation only. Attribution,
// license and change notices must remain with this derivative. No endorsement is
// implied.
//
// ## User-supplied recordings — owner-confirmed addon distribution
//
// On 2026-10-02 the owner explicitly confirmed permission for Workshop
// redistribution of the supplied recordings. The manifest records `user_attested`
// with addon permission, a source-bound confirmation and no source/preview grant.
// This is an owner assertion, not independent verification of original authorship
// or unidentified public-license terms. Original source hashes and preparation
// steps remain in the per-asset ledger. The original download folder is unchanged.
// The private repository retains [the confirmation](AUDIO_PERMISSION_CONFIRMATION.md).
//
// | Derivative | Supplied source |
// |---|---|
// | EAS_WarMix_Gunfire1 | 1 hour distant gunfire sound effect, 0-600 seconds |
// | EAS_WarMix_Gunfire2 | 1 hour distant gunfire sound effect, 600-1200 seconds |
// | EAS_WarMix_Outpost | Distant War Ambience / Artillery & Explosions / Night Watch Outpost |
// | EAS_Crowd_Angry | mixkit-angry-male-crowd-ambience-458.wav |
// | EAS_Crowd_Talking | mixkit-big-crowd-talking-loop-364.wav |
// | EAS_Crowd_Downtown | mixkit-city-downtown-crowd-ambience-361.wav |
// | EAS_Crowd_Large | mixkit-large-talking-crowd-ambience-981.wav |
// | EAS_Radio_Battlefield3 | Battlefield 3 Sounds - Radiochatter |
// | EAS_Radio_MysteryRussian | Mysterious Russian radio transmission |
// | EAS_Radio_RussianChatter | Russian Radio Chatter, Version 1 |
// | EAS_EmergencyAlert | United States Emergency Alert System Sound effect |
//
// The [Mixkit Sound Effects license](https://mixkit.co/license/modal/sfxFree/)
// permits incorporation into a larger video-game end product but prohibits
// standalone and source-file redistribution. These four exact supplied recordings
// have not independently been matched to the provider downloads; distribution of
// these supplied files relies on the owner's confirmation. Source-file restrictions
// remain. Other supplied filenames do not identify their precise upstream grants;
// the addon package license does not relicense their contents.
//
// All new supplied derivatives use 24 kHz mono PCM16. This reduces PCM storage;
// it is not an Opus/MP3 encoding. Complete durations are retained, with brief ramps
// and attenuation-only headroom. The two ten-minute gunfire sources and mystery
// Russian source contain original full-scale samples. No audible quality or
// seamless-loop certification is implied by conversion or numeric checks.
//
//
// Original code license:
// MIT License
//
// Copyright (c) 2026 ExpBG Tech
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//
// Scope notice: MIT applies to original EXPBG source and tooling. Included audio is
// separately licensed; see addon/EXPBG_Ambient_Sounds/Credits/AUDIO_CREDITS.txt.
// The combined distribution includes Vinny-derived APL-SA recordings and is subject
// to their attribution, noncommercial, Arma-only and share-alike conditions. This
// MIT license does not relicense those recordings or the independently licensed CC assets.
