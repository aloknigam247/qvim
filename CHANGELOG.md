# Changelog

## [0.2.0](https://github.com/aloknigam247/qvim/compare/v0.1.0...v0.2.0) (2026-09-21)


### Features

* add dockable chat panel with echo-stub responder ([#45](https://github.com/aloknigam247/qvim/issues/45)) ([c1996be](https://github.com/aloknigam247/qvim/commit/c1996be80d9e50cdea4281850ec020cfe64ca143)), closes [#28](https://github.com/aloknigam247/qvim/issues/28)
* Android companion app skeleton mirroring qvim chat over WebSocket ([#56](https://github.com/aloknigam247/qvim/issues/56)) ([ec40e8c](https://github.com/aloknigam247/qvim/commit/ec40e8c181b58482fc24491c2762af5c573aa8ff))
* **arguments:** accept vim-style argv via nvim forwarding ([af06e48](https://github.com/aloknigam247/qvim/commit/af06e48c547cf6b70fae96e22730f313c1e74b7b))
* automate versioning from conventional commits ([#81](https://github.com/aloknigam247/qvim/issues/81)) ([c33a165](https://github.com/aloknigam247/qvim/commit/c33a1659885aa536b279ce869e51738a239997a7))
* **cli:** support `qvim -` to read stdin into the first buffer ([10a8a08](https://github.com/aloknigam247/qvim/commit/10a8a08bef5f7a7fdcfc63aac4a09d3cf22761bc))
* **config:** add config framework (g: globals + --qvim-* CLI) ([69f3688](https://github.com/aloknigam247/qvim/commit/69f368859d8abbd0775c0e4d0933bd3808aec70a))
* copilot-bridge chat backend with mobile relay ([#63](https://github.com/aloknigam247/qvim/issues/63)) ([5cb3bad](https://github.com/aloknigam247/qvim/commit/5cb3bad14a6be4edf7c9cbf51d0fd2bb8dd57edf))
* **cursor:** ease cursor moves with an 80ms OutExpo animation ([0c39d68](https://github.com/aloknigam247/qvim/commit/0c39d68c0c8e4d4100aa3b68fcd548cc917de175))
* **cursor:** snap on cursor jumps &gt; 1 cell, ease only on adjacent moves ([25682d4](https://github.com/aloknigam247/qvim/commit/25682d4aa43457d30bce2e59a582d40f2b38f72d))
* enable harfbuzz in qtbase vcpkg build for ligature support ([293677b](https://github.com/aloknigam247/qvim/commit/293677bed3e825fa075e759acd6ec0f36d3ed815))
* ext_multigrid + ext_messages + perf + smoke harness + fixes ([50d22e0](https://github.com/aloknigam247/qvim/commit/50d22e026a8d153f835103928ffc2bb6f2bdf976))
* **hl:** enable ext_hlstate and flag Visual-derived attributes ([b4f4e01](https://github.com/aloknigam247/qvim/commit/b4f4e019ec6c3f4c4de34553b78e0ed95d6705f1))
* **hl:** make rounded-pill highlight groups configurable ([0761cba](https://github.com/aloknigam247/qvim/commit/0761cba50eded5292ca466c4c19cc6cb61ed83c7))
* identify qvim to nvim + drive overlay fonts from guifont ([9c65f03](https://github.com/aloknigam247/qvim/commit/9c65f03849795d2edba44d6da306b0564a7af318))
* initial qvim — Neovim GUI client in C++23 / Qt 6.10 / QML ([aca52dd](https://github.com/aloknigam247/qvim/commit/aca52dd573ece5887d3ea53ea6e385f91eaf9bed))
* mDNS LAN discovery for the session mirror ([#62](https://github.com/aloknigam247/qvim/issues/62)) ([1385eed](https://github.com/aloknigam247/qvim/commit/1385eed3ceb43ab7f4cde1201e3e9f2f1d01b86b))
* mirror the chat panel session over ws:// while the panel is open ([#58](https://github.com/aloknigam247/qvim/issues/58)) ([2eebda9](https://github.com/aloknigam247/qvim/commit/2eebda9090713364553adf8f8f0f21ef2275da47))
* **mouse:** horizontal scroll wheel support ([#23](https://github.com/aloknigam247/qvim/issues/23)) ([53acf98](https://github.com/aloknigam247/qvim/commit/53acf98da4aa32659ad047b5d63cf2ed725d9adf)), closes [#1](https://github.com/aloknigam247/qvim/issues/1)
* **paint:** inverse-round concave corners on multi-row selections ([94c8651](https://github.com/aloknigam247/qvim/commit/94c865199f0555b64691162a4641e6729d905e67))
* **paint:** round Visual selection corners ([05e6702](https://github.com/aloknigam247/qvim/commit/05e6702c18071bb1fe09b7c0c6281b66299c80d4))
* resize window when nvim sets columns/lines ([#84](https://github.com/aloknigam247/qvim/issues/84)) ([65dea03](https://github.com/aloknigam247/qvim/commit/65dea03f2e97054471257d547c70dc1d7857d043))
* session cache to eliminate window snap padding on launch ([6bbdb14](https://github.com/aloknigam247/qvim/commit/6bbdb14b7e0b8465f9a01d5615568afeb9cac7f9))
* **triage:** check for duplicate/related issues and add spike category ([3399785](https://github.com/aloknigam247/qvim/commit/33997857871d85d21477fb37d984840d23db69fc))
* **ui:** add app icon ([031e929](https://github.com/aloknigam247/qvim/commit/031e9293693a7f33a3442ddc69982cf5e49f37d1))
* **ui:** F11 shortcut toggles fullscreen, pre-resizes grid ([e8c8687](https://github.com/aloknigam247/qvim/commit/e8c86877bd1034d8410293f143250c11dfc6e8a8))
* **ui:** match title bar to vim background ([cbb21e5](https://github.com/aloknigam247/qvim/commit/cbb21e5d68b78d64e04d1fa067b3d5d050ac7f92))
* **ui:** show file path in title bar ([58beebc](https://github.com/aloknigam247/qvim/commit/58beebcf92f1ab37e81280b51a65435483e3af32))
* **vim_options:** honour option_set changes live ([71b77dc](https://github.com/aloknigam247/qvim/commit/71b77dc912c86410b5432a68f41b2fe5dc574944))


### Bug Fixes

* activate qvim window on first show ([#71](https://github.com/aloknigam247/qvim/issues/71)) ([dec52da](https://github.com/aloknigam247/qvim/commit/dec52da853e8c1fb834ca71e3110ace3853fa62d))
* **arg:** make `qvim -` work reliably; document pwsh quirk ([083ccf4](https://github.com/aloknigam247/qvim/commit/083ccf41a6602bde32d565fd37db121c47ff7a82))
* **arguments:** unmangle PowerShell-crushed argv on Windows ([38c01ce](https://github.com/aloknigam247/qvim/commit/38c01ce160df3038739462b11dd4f98d0cab1aa2))
* attach parent console for -h/-v output ([#77](https://github.com/aloknigam247/qvim/issues/77)) ([a8c3f58](https://github.com/aloknigam247/qvim/commit/a8c3f58065e82dda84d8438b4155f291c308b3f1))
* **boot:** hide window until grid resize completes to remove visible resize jump ([0af9f3b](https://github.com/aloknigam247/qvim/commit/0af9f3bfaf2ae87ce259dd043992df6b21744b47))
* **build:** silence all compile warnings under /W4 ([f6d4fdd](https://github.com/aloknigam247/qvim/commit/f6d4fdddc7b43fe418d75d5a234c474e324f37a6))
* **config:** default rounded_highlights to empty (opt-in) ([8e89717](https://github.com/aloknigam247/qvim/commit/8e897173967820bada5ac2a62cc7030d1a0b7810))
* **cursor:** draw underline at cell bottom matching grid paint ([5845f97](https://github.com/aloknigam247/qvim/commit/5845f9741420f935087cd28d41ae672701b6ccb2))
* **cursor:** fall back to target cell on initial paint ([0c29ef1](https://github.com/aloknigam247/qvim/commit/0c29ef1220721a3fa817b8e612a07f35c78f26e0))
* **cursor:** preserve italic/bold of target cell in carry glyph ([95ec25b](https://github.com/aloknigam247/qvim/commit/95ec25b847194afcba7ecd5ad43734c58606d335))
* **cursor:** suppress blink during cursor activity ([299d7e2](https://github.com/aloknigam247/qvim/commit/299d7e239e112d487f50574b9edd0869d8c44d05))
* **font:** make FontFallback thread-safe for SceneGraph paint ([a086f48](https://github.com/aloknigam247/qvim/commit/a086f48da4b07a8b0aeecb7f98446526036da59e))
* **font:** render nerd font glyphs from primary face ([d905d95](https://github.com/aloknigam247/qvim/commit/d905d9587f6501cca6926eaaa839083f2b637a3b))
* **font:** resize nvim grid to fit window on guifont/linespace change ([d655164](https://github.com/aloknigam247/qvim/commit/d6551640acf0ad26a20d8e0314c1bf96930b3421))
* handle nvim :restart ui event ([#72](https://github.com/aloknigam247/qvim/issues/72)) ([f0c093b](https://github.com/aloknigam247/qvim/commit/f0c093b67a7ea3928615f6f9d16b2b69f6241e63))
* **input:** preserve Shift for Ctrl+Shift+letter ([9c9f490](https://github.com/aloknigam247/qvim/commit/9c9f49052b2369e5431f771e3618deaa119816e2))
* **input:** preserve Shift for Ctrl+Shift+letter ([1d15894](https://github.com/aloknigam247/qvim/commit/1d15894469a3b42026e8f137ec2a62c18a8fc5d7)), closes [#14](https://github.com/aloknigam247/qvim/issues/14)
* keep resized grid size across nvim restart ([#80](https://github.com/aloknigam247/qvim/issues/80)) ([dab7bcb](https://github.com/aloknigam247/qvim/commit/dab7bcb754fd501ceaa2def5037e9d21b17eb2da)), closes [#78](https://github.com/aloknigam247/qvim/issues/78)
* **mouse:** start visual mode on drag, not release ([5f205eb](https://github.com/aloknigam247/qvim/commit/5f205eb835e76b4b19bbda59005373c4482a8ea4))
* paint ambient background behind rounded highlights ([c8b562c](https://github.com/aloknigam247/qvim/commit/c8b562c86eb6389efc21a168205aebbec8143586))
* paint ambient background behind rounded highlights ([c9132fc](https://github.com/aloknigam247/qvim/commit/c9132fca18f7cec2c1936423fb6f358a8ef875df)), closes [#17](https://github.com/aloknigam247/qvim/issues/17)
* **paint:** render nerd-font PUA glyphs via QGlyphRun overlay ([4338c10](https://github.com/aloknigam247/qvim/commit/4338c106fb79920fab2deb21d66803ba8ab906d0))
* **paint:** snap cell width and height to integer pixels ([4d1cf95](https://github.com/aloknigam247/qvim/commit/4d1cf95d473e94c5cb4221cf933553f54dfa9ab1))
* **qml:** exclude grid 1 from sub-grid Repeater ([23afe5f](https://github.com/aloknigam247/qvim/commit/23afe5ffc2b707080e7358b27af4d1611d5e317b))
* **qml:** launch window at 1200x780 to match user guifont grid ([809f3b5](https://github.com/aloknigam247/qvim/commit/809f3b5e43a30e852e476589b3d550900315cb48))
* re-fit grid after font change on restart ([#82](https://github.com/aloknigam247/qvim/issues/82)) ([255a4d0](https://github.com/aloknigam247/qvim/commit/255a4d00b60e5e1656d193cb0c739275588b3b97))
* render the grid through the Qt Quick scene graph ([#25](https://github.com/aloknigam247/qvim/issues/25)) ([0b9bb65](https://github.com/aloknigam247/qvim/commit/0b9bb653be8887084857d2184b62b87b190a05bf))
* **rendering:** paint underline at cell bottom to meet indentline ([49da7f0](https://github.com/aloknigam247/qvim/commit/49da7f02fde021266659c5e3401e66c1e9e50027))
* **rendering:** pin font letter-spacing to cellWidth to align text with cell grid ([8245492](https://github.com/aloknigam247/qvim/commit/82454929ecd8dba5b5664311bce9fb9901b3f971))
* **rendering:** snap cell metrics to device pixels to eliminate inter-row gaps ([c42ce24](https://github.com/aloknigam247/qvim/commit/c42ce2498c55e0930fac6086f3efba8392d5a4e1))
* **rendering:** split rounded highlight spans and polygons by bg color ([1c5817d](https://github.com/aloknigam247/qvim/commit/1c5817d57ef0412bcd484334707595b6f052e205))
* send grid=0 in nvim_input_mouse when ext_multigrid is off ([fbd6eb4](https://github.com/aloknigam247/qvim/commit/fbd6eb45e3a6ad7a504086b5e77a920c8b6cb443))
* **text:** honour bold attribute per highlight ([ea6ede1](https://github.com/aloknigam247/qvim/commit/ea6ede1e85cea2e2161db7a8e1defc1c56fcef99))
* **ui:** coalesce resize RPCs for smooth drag ([b26a098](https://github.com/aloknigam247/qvim/commit/b26a0985f93dcb91b4e0eb444fb8ac2256be3037))
* **ui:** drop resize debounce to 0ms (immediate) ([01131ad](https://github.com/aloknigam247/qvim/commit/01131ad050ed0b8c1c397185021b4841924370e5))
* **window:** focus float on mouse click ([4c7ef73](https://github.com/aloknigam247/qvim/commit/4c7ef737f9b6b0b66792e8b95d3970619ba04ecd))


### Performance Improvements

* **boot:** fire nvim_ui_attach early so RPC overlaps with QML cold load ([41259dc](https://github.com/aloknigam247/qvim/commit/41259dca61ec1b6879b3b32c9abc2dfb8fb61f7c))
* **grid:** skip GridItem repaint on flushes that touched no cells ([35158c5](https://github.com/aloknigam247/qvim/commit/35158c5f68316b9b6585535637fbf8df516de389))
* **launch:** exclude whole build tree from Defender, not just deploy dir ([#46](https://github.com/aloknigam247/qvim/issues/46)) ([08b47ea](https://github.com/aloknigam247/qvim/commit/08b47ea152dabaf2c8027e4a612a7a1e01b3d0c7))
* **launch:** FreeConsole after stdin slurp to detach from parent shell ([66b6f20](https://github.com/aloknigam247/qvim/commit/66b6f2070735b2260f4d01c4567b3d8c18c1206a))
* **launch:** trim Qt deploy and add Defender-exclusion helper for cold start ([#39](https://github.com/aloknigam247/qvim/issues/39)) ([f7e87a4](https://github.com/aloknigam247/qvim/commit/f7e87a4b2d7fa983ef4fb07134a58de23b47b6de))
* **scroll:** rotate row handles instead of copying every cell ([b6341f1](https://github.com/aloknigam247/qvim/commit/b6341f104b822abf9242cbfbfe3a5a1fc8f1aa98))


### Reverts

* drop font-fallback / bold-fix commits ([a48d9bc](https://github.com/aloknigam247/qvim/commit/a48d9bc5d0ba586121d3ff81ff25940f310cccf5))
