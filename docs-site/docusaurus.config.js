// @ts-check

import { themes as prismThemes } from 'prism-react-renderer';

/** @type {import('@docusaurus/types').Config} */
const config = {
    title: 'Cue Engine Docs',
    tagline: 'Reference and manual for Cue Engine',
    favicon: 'img/favicon.ico',

    future: {
        v4: true,
    },

    url: 'https://tochouseito.github.io',
    baseUrl: '/CueEngine/',

    organizationName: 'tochouseito',
    projectName: 'CueEngine',

    onBrokenLinks: 'throw',
    onBrokenMarkdownLinks: 'warn',

    i18n: {
        defaultLocale: 'ja',
        locales: ['ja'],
    },

    presets: [
        [
            'classic',
            /** @type {import('@docusaurus/preset-classic').Options} */
            ({
                docs: {
                    sidebarPath: './sidebars.js',
                    routeBasePath: 'docs',
                    editUrl: 'https://github.com/tochouseito/CueEngine/tree/release/docs-site/',
                },
                blog: false,
                theme: {
                    customCss: './src/css/custom.css',
                },
            }),
        ],
    ],

    themeConfig:
        /** @type {import('@docusaurus/preset-classic').ThemeConfig} */
        ({
            colorMode: {
                respectPrefersColorScheme: true,
            },

            navbar: {
                title: 'Cue Engine',
                items: [
                    {
                        type: 'docSidebar',
                        sidebarId: 'tutorialSidebar',
                        position: 'left',
                        label: 'Docs',
                    },
                    {
                        href: 'https://github.com/tochouseito/CueEngine',
                        label: 'GitHub',
                        position: 'right',
                    },
                ],
            },

            footer: {
                style: 'dark',
                links: [
                    {
                        title: 'Docs',
                        items: [
                            {
                                label: 'Introduction',
                                to: '/docs/intro',
                            },
                        ],
                    },
                    {
                        title: 'Project',
                        items: [
                            {
                                label: 'GitHub',
                                href: 'https://github.com/tochouseito/CueEngine',
                            },
                        ],
                    },
                ],
                copyright: `Copyright © ${new Date().getFullYear()} Cue Engine`,
            },

            prism: {
                theme: prismThemes.github,
                darkTheme: prismThemes.dracula,
            },
        }),
};

export default config;
