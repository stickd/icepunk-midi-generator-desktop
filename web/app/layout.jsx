import "./globals.css";
export const metadata = {
  title: "ICEPUNK · Admin",
  description: "Private licensing administration",
  robots: { index: false, follow: false },
};
export default function RootLayout({ children }) {
  return <html lang="en"><body><main>{children}</main></body></html>;
}

